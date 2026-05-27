#pragma once

#include <flowq/error.hpp>
#include <flowq/quic/tls_handshake.hpp>
#include <flowq/quic/tls_provider_backend.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

#if defined(FLOWQ_ENABLE_OPENSSL_QUIC_TLS)
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

namespace flowq::quic {

/// Configuration for OpenSSL QUIC TLS handshake adapter.
struct openssl_tls_config {
    bool is_client{true};
    const char* cert_file{};    // Server: path to certificate file
    const char* key_file{};     // Server: path to private key file
    const char* ca_file{};      // Client: path to CA certificate for verification
};

#if defined(FLOWQ_ENABLE_OPENSSL_QUIC_TLS)

/// OpenSSL 3.5+ QUIC TLS handshake adapter.
/// Implements tls_handshake_adapter using SSL_set_quic_tls_cbs() callback dispatch.
class openssl_tls_handshake_adapter final : public tls_handshake_adapter {
public:
    explicit openssl_tls_handshake_adapter(openssl_tls_config config)
        : config_{config} {
        // Create SSL context
        ctx_ = SSL_CTX_new(TLS_method());
        if (ctx_ == nullptr) {
            state_ = handshake_state::failed;
            return;
        }

        // Force TLS 1.3 only
        SSL_CTX_set_min_proto_version(ctx_, TLS1_3_VERSION);
        SSL_CTX_set_max_proto_version(ctx_, TLS1_3_VERSION);

        // Configure certificates
        if (!config.is_client && config.cert_file != nullptr) {
            SSL_CTX_use_certificate_chain_file(ctx_, config.cert_file);
            if (config.key_file != nullptr) {
                SSL_CTX_use_PrivateKey_file(ctx_, config.key_file, SSL_FILETYPE_PEM);
            }
        }

        // Configure CA for client verification
        if (config.is_client && config.ca_file != nullptr) {
            SSL_CTX_load_verify_locations(ctx_, config.ca_file, nullptr);
            SSL_CTX_set_verify(ctx_, SSL_VERIFY_PEER, nullptr);
        }

        // Create SSL object
        ssl_ = SSL_new(ctx_);
        if (ssl_ == nullptr) {
            state_ = handshake_state::failed;
            return;
        }

        // Set client/server mode
        if (config.is_client) {
            SSL_set_connect_state(ssl_);
        } else {
            SSL_set_accept_state(ssl_);
        }

        // Register QUIC TLS callbacks
        if (SSL_set_quic_tls_cbs(ssl_, quic_tls_dispatch_table(), this) != 1) {
            state_ = handshake_state::failed;
            return;
        }
    }

    ~openssl_tls_handshake_adapter() override {
        if (ssl_ != nullptr) {
            SSL_free(ssl_);
        }
        if (ctx_ != nullptr) {
            SSL_CTX_free(ctx_);
        }
    }

    // Non-copyable, non-movable
    openssl_tls_handshake_adapter(const openssl_tls_handshake_adapter&) = delete;
    openssl_tls_handshake_adapter& operator=(const openssl_tls_handshake_adapter&) = delete;
    openssl_tls_handshake_adapter(openssl_tls_handshake_adapter&&) = delete;
    openssl_tls_handshake_adapter& operator=(openssl_tls_handshake_adapter&&) = delete;

    [[nodiscard]] handshake_state state() const noexcept override {
        return state_;
    }

    [[nodiscard]] tls_key_availability key_availability() const noexcept override {
        return keys_;
    }

    [[nodiscard]] flowq::error receive_crypto(crypto_bytes bytes) override {
        if (state_ == handshake_state::failed) {
            return flowq::error{flowq::error_code::tls_error, "TLS handshake already failed"};
        }

        // Append bytes to the appropriate inbound buffer
        switch (bytes.level) {
        case tls_encryption_level::initial:
            initial_inbound_.insert(initial_inbound_.end(),
                reinterpret_cast<const std::byte*>(bytes.data.data()),
                reinterpret_cast<const std::byte*>(bytes.data.data()) + bytes.data.size());
            break;
        case tls_encryption_level::handshake:
            handshake_inbound_.insert(handshake_inbound_.end(),
                reinterpret_cast<const std::byte*>(bytes.data.data()),
                reinterpret_cast<const std::byte*>(bytes.data.data()) + bytes.data.size());
            break;
        case tls_encryption_level::application:
            application_inbound_.insert(application_inbound_.end(),
                reinterpret_cast<const std::byte*>(bytes.data.data()),
                reinterpret_cast<const std::byte*>(bytes.data.data()) + bytes.data.size());
            break;
        }

        return drive_handshake();
    }

    [[nodiscard]] std::vector<crypto_bytes> drain_crypto() override {
        auto result = std::move(outbound_crypto_);
        outbound_crypto_.clear();
        return result;
    }

    [[nodiscard]] const std::vector<std::byte>& peer_transport_params() const noexcept {
        return peer_transport_params_;
    }

private:
    // OpenSSL callback: send CRYPTO data to QUIC layer
    static int crypto_send_cb(SSL* /*ssl*/, const unsigned char* buf, size_t len, size_t* consumed, void* arg) {
        auto* self = static_cast<openssl_tls_handshake_adapter*>(arg);

        // Determine current encryption level for outbound data
        auto level = self->current_tx_level_;

        // Get offset for this level
        std::uint64_t offset{};
        switch (level) {
        case tls_encryption_level::initial:
            offset = self->initial_offset_;
            self->initial_offset_ += len;
            break;
        case tls_encryption_level::handshake:
            offset = self->handshake_offset_;
            self->handshake_offset_ += len;
            break;
        case tls_encryption_level::application:
            offset = self->application_offset_;
            self->application_offset_ += len;
            break;
        }

        // Create buffer copy
        flowq::buffer data{std::span<const std::byte>{
            reinterpret_cast<const std::byte*>(buf), len}};

        self->outbound_crypto_.push_back(crypto_bytes{level, offset, std::move(data)});

        // Full consumption required by OpenSSL 3.5
        *consumed = len;
        return 1;
    }

    // OpenSSL callback: provide inbound CRYPTO data to TLS
    static int crypto_recv_rcd_cb(SSL* /*ssl*/, const unsigned char** buf, size_t* bytes_read, void* arg) {
        auto* self = static_cast<openssl_tls_handshake_adapter*>(arg);

        // Try each level in order: initial, handshake, application
        if (self->initial_read_pos_ < self->initial_inbound_.size()) {
            *buf = reinterpret_cast<const unsigned char*>(
                self->initial_inbound_.data() + self->initial_read_pos_);
            *bytes_read = self->initial_inbound_.size() - self->initial_read_pos_;
            return 1;
        }

        if (self->handshake_read_pos_ < self->handshake_inbound_.size()) {
            *buf = reinterpret_cast<const unsigned char*>(
                self->handshake_inbound_.data() + self->handshake_read_pos_);
            *bytes_read = self->handshake_inbound_.size() - self->handshake_read_pos_;
            return 1;
        }

        if (self->application_read_pos_ < self->application_inbound_.size()) {
            *buf = reinterpret_cast<const unsigned char*>(
                self->application_inbound_.data() + self->application_read_pos_);
            *bytes_read = self->application_inbound_.size() - self->application_read_pos_;
            return 1;
        }

        // No data available
        *buf = nullptr;
        *bytes_read = 0;
        return 1;
    }

    // OpenSSL callback: release consumed inbound CRYPTO data
    static int crypto_release_rcd_cb(SSL* /*ssl*/, size_t bytes_read, void* arg) {
        auto* self = static_cast<openssl_tls_handshake_adapter*>(arg);

        // Advance read position for the appropriate level
        if (self->initial_read_pos_ < self->initial_inbound_.size()) {
            self->initial_read_pos_ += bytes_read;
            if (self->initial_read_pos_ >= self->initial_inbound_.size()) {
                self->initial_inbound_.clear();
                self->initial_read_pos_ = 0;
            }
            return 1;
        }

        if (self->handshake_read_pos_ < self->handshake_inbound_.size()) {
            self->handshake_read_pos_ += bytes_read;
            if (self->handshake_read_pos_ >= self->handshake_inbound_.size()) {
                self->handshake_inbound_.clear();
                self->handshake_read_pos_ = 0;
            }
            return 1;
        }

        if (self->application_read_pos_ < self->application_inbound_.size()) {
            self->application_read_pos_ += bytes_read;
            if (self->application_read_pos_ >= self->application_inbound_.size()) {
                self->application_inbound_.clear();
                self->application_read_pos_ = 0;
            }
            return 1;
        }

        return 1;
    }

    // OpenSSL callback: receive traffic secret for key export
    static int yield_secret_cb(SSL* /*ssl*/, uint32_t prot_level, int direction,
                               const unsigned char* /*secret*/, size_t /*len*/, void* arg) {
        auto* self = static_cast<openssl_tls_handshake_adapter*>(arg);

        auto level = map_protection_level(prot_level);

        // Update key availability
        switch (level) {
        case tls_encryption_level::initial:
            if (direction == 0) self->keys_.initial = true;  // RX
            break;
        case tls_encryption_level::handshake:
            if (direction == 0) self->keys_.handshake = true;  // RX
            break;
        case tls_encryption_level::application:
            if (direction == 0) self->keys_.application = true;  // RX
            break;
        }

        // Update TX level for outbound CRYPTO classification
        if (direction == 1) {  // TX
            self->update_tx_level(level);
        }

        // Transition to handshaking on first non-initial secret
        if (level != tls_encryption_level::initial && self->state_ == handshake_state::idle) {
            self->state_ = handshake_state::handshaking;
        }

        return 1;
    }

    // OpenSSL callback: receive peer transport parameters
    static int got_transport_params_cb(SSL* /*ssl*/, const unsigned char* params, size_t len, void* arg) {
        auto* self = static_cast<openssl_tls_handshake_adapter*>(arg);
        self->peer_transport_params_.assign(
            reinterpret_cast<const std::byte*>(params),
            reinterpret_cast<const std::byte*>(params) + len);
        return 1;
    }

    // OpenSSL callback: receive TLS alert
    static int alert_cb(SSL* /*ssl*/, uint8_t alert_code, void* arg) {
        auto* self = static_cast<openssl_tls_handshake_adapter*>(arg);
        self->alert_received_ = true;
        self->alert_code_ = alert_code;
        self->state_ = handshake_state::failed;
        return 1;
    }

    // Build the OSSL_DISPATCH table for SSL_set_quic_tls_cbs
    static const OSSL_DISPATCH* quic_tls_dispatch_table() noexcept {
        // Callback IDs from openssl/core_dispatch.h
        static const OSSL_DISPATCH table[] = {
            {2001, reinterpret_cast<OSSL_FUNC*>(crypto_send_cb)},
            {2002, reinterpret_cast<OSSL_FUNC*>(crypto_recv_rcd_cb)},
            {2003, reinterpret_cast<OSSL_FUNC*>(crypto_release_rcd_cb)},
            {2004, reinterpret_cast<OSSL_FUNC*>(yield_secret_cb)},
            {2005, reinterpret_cast<OSSL_FUNC*>(got_transport_params_cb)},
            {2006, reinterpret_cast<OSSL_FUNC*>(alert_cb)},
            {0, nullptr}  // sentinel
        };
        return table;
    }

    // Map OpenSSL protection level to FlowQ encryption level
    [[nodiscard]] static tls_encryption_level map_protection_level(uint32_t prot_level) noexcept {
        // OSSL_RECORD_PROTECTION_LEVEL_* constants
        switch (prot_level) {
        case 0: return tls_encryption_level::initial;      // NONE
        case 1: return tls_encryption_level::handshake;     // HANDSHAKE
        case 2: return tls_encryption_level::application;   // APPLICATION
        default: return tls_encryption_level::initial;
        }
    }

    // Update current TX level
    void update_tx_level(tls_encryption_level level) noexcept {
        current_tx_level_ = level;
    }

    // Drive handshake after receiving new CRYPTO bytes
    [[nodiscard]] flowq::error drive_handshake() {
        if (ssl_ == nullptr) {
            return flowq::error{flowq::error_code::tls_error, "SSL object not initialized"};
        }

        // Drive OpenSSL handshake
        int rv = SSL_do_handshake(ssl_);
        if (rv <= 0) {
            int err = SSL_get_error(ssl_, rv);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                // Normal - need more data
                return {};
            }
            state_ = handshake_state::failed;
            unsigned long openssl_err = ERR_get_error();
            char err_buf[256]{};
            ERR_error_string_n(openssl_err, err_buf, sizeof(err_buf));
            return flowq::error{flowq::error_code::tls_error, err_buf};
        }

        // Post-handshake read to trigger final key export
        SSL_read(ssl_, nullptr, 0);

        // Check if handshake is complete
        if (SSL_is_init_finished(ssl_)) {
            state_ = handshake_state::handshake_confirmed;
        }

        return {};
    }

    // OpenSSL objects (owned, RAII)
    SSL_CTX* ctx_{};
    SSL* ssl_{};

    // Adapter state
    handshake_state state_{handshake_state::idle};
    tls_key_availability keys_{};
    tls_encryption_level current_tx_level_{tls_encryption_level::initial};

    // Outbound CRYPTO buffer
    std::vector<crypto_bytes> outbound_crypto_;
    std::uint64_t initial_offset_{};
    std::uint64_t handshake_offset_{};
    std::uint64_t application_offset_{};

    // Inbound CRYPTO buffers (per level)
    std::vector<std::byte> initial_inbound_;
    std::size_t initial_read_pos_{};
    std::vector<std::byte> handshake_inbound_;
    std::size_t handshake_read_pos_{};
    std::vector<std::byte> application_inbound_;
    std::size_t application_read_pos_{};

    // Peer transport parameters
    std::vector<std::byte> peer_transport_params_;

    // Alert state
    bool alert_received_{};
    uint8_t alert_code_{};

    // Config
    openssl_tls_config config_;
};

#else  // !FLOWQ_ENABLE_OPENSSL_QUIC_TLS

// Stub implementation when OpenSSL QUIC TLS is disabled
class openssl_tls_handshake_adapter final : public tls_handshake_adapter {
public:
    explicit openssl_tls_handshake_adapter(openssl_tls_config) {}
    ~openssl_tls_handshake_adapter() override = default;

    [[nodiscard]] handshake_state state() const noexcept override { return handshake_state::idle; }
    [[nodiscard]] tls_key_availability key_availability() const noexcept override { return {}; }
    [[nodiscard]] flowq::error receive_crypto(crypto_bytes) override {
        return flowq::error{flowq::error_code::tls_error, "OpenSSL QUIC TLS backend is disabled"};
    }
    [[nodiscard]] std::vector<crypto_bytes> drain_crypto() override { return {}; }
    [[nodiscard]] const std::vector<std::byte>& peer_transport_params() const noexcept {
        static const std::vector<std::byte> empty;
        return empty;
    }
};

#endif  // FLOWQ_ENABLE_OPENSSL_QUIC_TLS

} // namespace flowq::quic
