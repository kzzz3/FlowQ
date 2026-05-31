#pragma once

#include <flowq/error.hpp>
#include <flowq/quic/tls_handshake.hpp>
#include <flowq/quic/tls_provider_backend.hpp>
#include <flowq/quic/transport_parameters.hpp>

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(FLOWQ_ENABLE_OPENSSL_QUIC_TLS)
#include <openssl/core_dispatch.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/tls1.h>
#endif

namespace flowq::quic {

/// Configuration for OpenSSL QUIC TLS handshake adapter.
struct openssl_tls_config {
    bool is_client{true};
    const char* cert_file{};    // Server: path to certificate file
    const char* key_file{};     // Server: path to private key file
    const char* ca_file{};      // Client: path to CA certificate for verification
    const char* alpn{"hq-interop"};
    const char* tls13_ciphersuite{"TLS_AES_128_GCM_SHA256"};
    transport_parameters local_transport_parameters{
        .max_udp_payload_size = 1200,
        .initial_max_data = 1048576,
        .initial_max_stream_data_bidi_local = 262144,
        .initial_max_stream_data_bidi_remote = 262144,
        .initial_max_stream_data_uni = 262144,
        .initial_max_streams_bidi = 128,
        .initial_max_streams_uni = 128,
        .active_connection_id_limit = 2
    };
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
            set_failure("failed to create OpenSSL SSL_CTX");
            return;
        }

        // Force TLS 1.3 only
        SSL_CTX_set_min_proto_version(ctx_, TLS1_3_VERSION);
        SSL_CTX_set_max_proto_version(ctx_, TLS1_3_VERSION);
        if (SSL_CTX_set_ciphersuites(ctx_, config_.tls13_ciphersuite) != 1) {
            set_failure("failed to configure TLS 1.3 cipher suite");
            return;
        }

        if (!config.is_client) {
            if (config.cert_file == nullptr || config.key_file == nullptr) {
                set_failure("server TLS requires certificate chain and private key");
                return;
            }
            if (SSL_CTX_use_certificate_chain_file(ctx_, config.cert_file) != 1) {
                set_failure("failed to load server certificate chain");
                return;
            }
            if (SSL_CTX_use_PrivateKey_file(ctx_, config.key_file, SSL_FILETYPE_PEM) != 1) {
                set_failure("failed to load server private key");
                return;
            }
            if (SSL_CTX_check_private_key(ctx_) != 1) {
                set_failure("server certificate and private key do not match");
                return;
            }
        }

        if (config.is_client && config.ca_file != nullptr) {
            if (SSL_CTX_load_verify_locations(ctx_, config.ca_file, nullptr) != 1) {
                set_failure("failed to load client CA certificate");
                return;
            }
            SSL_CTX_set_verify(ctx_, SSL_VERIFY_PEER, nullptr);
        }

        // Create SSL object
        ssl_ = SSL_new(ctx_);
        if (ssl_ == nullptr) {
            set_failure("failed to create OpenSSL SSL object");
            return;
        }

        // Set client/server mode
        if (config.is_client) {
            SSL_set_connect_state(ssl_);
        } else {
            SSL_set_accept_state(ssl_);
        }

        if (config_.alpn != nullptr) {
            const auto alpn_length = std::strlen(config_.alpn);
            if (alpn_length == 0 || alpn_length > 255) {
                set_failure("invalid ALPN length");
                return;
            }
            std::vector<unsigned char> alpn;
            alpn.push_back(static_cast<unsigned char>(alpn_length));
            alpn.insert(alpn.end(), config_.alpn, config_.alpn + static_cast<std::ptrdiff_t>(alpn_length));
            if (SSL_set_alpn_protos(ssl_, alpn.data(), static_cast<unsigned int>(alpn.size())) != 0) {
                set_failure("failed to configure client ALPN");
                return;
            }
        }

        // Register QUIC TLS callbacks
        if (SSL_set_quic_tls_cbs(ssl_, quic_tls_dispatch_table(), this) != 1) {
            set_failure("failed to configure OpenSSL QUIC TLS callbacks");
            return;
        }

        auto encoded_local_params = encode_transport_parameters(config_.local_transport_parameters);
        if (!encoded_local_params.ok()) {
            set_failure(encoded_local_params.error.message());
            return;
        }
        local_transport_params_ = std::move(encoded_local_params.payload);
        if (SSL_set_quic_tls_transport_params(
            ssl_,
            reinterpret_cast<const unsigned char*>(local_transport_params_.data()),
            local_transport_params_.size()) != 1) {
            set_failure("failed to configure QUIC transport parameters");
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

    [[nodiscard]] const flowq::error& last_error() const noexcept {
        return last_error_;
    }

    [[nodiscard]] crypto_provider_status provider_status() const noexcept override {
        const auto suite = negotiated_cipher_suite();
        if (!level_key_schedule_owned(tls_encryption_level::application) || suite == cipher_suite::unknown) {
            return crypto_provider_status::unavailable();
        }
        return crypto_provider_status::available(
            suite,
            crypto_capabilities{false, false, false, false, true});
    }

    [[nodiscard]] flowq::error receive_crypto(crypto_bytes bytes) override {
        if (state_ == handshake_state::failed) {
            if (!last_error_.ok()) {
                return last_error_;
            }
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

    [[nodiscard]] flowq::error advance() override {
        if (state_ == handshake_state::failed) {
            if (!last_error_.ok()) {
                return last_error_;
            }
            return flowq::error{flowq::error_code::tls_error, "TLS handshake already failed"};
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

    /// Get traffic secret for a specific encryption level and direction.
    /// @param level The encryption level (initial, handshake, application)
    /// @param is_tx true for transmit secret, false for receive secret
    /// @return Reference to the secret bytes, empty if not yet available
    [[nodiscard]] const std::vector<std::byte>& traffic_secret(
        tls_encryption_level level, bool is_tx) const noexcept {
        return const_cast<openssl_tls_handshake_adapter*>(this)->secret_for(level, is_tx ? 1 : 0);
    }

    /// Check if traffic secret is available for a specific level and direction.
    [[nodiscard]] bool has_traffic_secret(tls_encryption_level level, bool is_tx) const noexcept {
        return !traffic_secret(level, is_tx).empty();
    }

    /// Get negotiated cipher suite.
    [[nodiscard]] cipher_suite negotiated_cipher() const noexcept {
        return negotiated_cipher_suite();
    }

    [[nodiscard]] static std::optional<tls_encryption_level> tls_level_for_openssl_protection_level(
        uint32_t prot_level) noexcept {
        return map_protection_level(prot_level);
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
                               const unsigned char* secret, size_t len, void* arg) {
        auto* self = static_cast<openssl_tls_handshake_adapter*>(arg);

        auto mapped_level = map_protection_level(prot_level);
        if (!mapped_level.has_value() || (direction != 0 && direction != 1) || secret == nullptr || len == 0) {
            self->set_failure("OpenSSL yielded invalid QUIC traffic secret metadata");
            return 0;
        }
        auto level = *mapped_level;

        const auto* first = reinterpret_cast<const std::byte*>(secret);
        auto& stored_secret = self->secret_for(level, direction);
        stored_secret.assign(first, first + len);

        // Update key availability
        switch (level) {
        case tls_encryption_level::initial:
            self->keys_.initial = self->level_key_schedule_owned(level);
            break;
        case tls_encryption_level::handshake:
            self->keys_.handshake = self->level_key_schedule_owned(level);
            break;
        case tls_encryption_level::application:
            self->keys_.application = self->level_key_schedule_owned(level);
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
    static int alert_cb(SSL* /*ssl*/, unsigned char alert_code, void* arg) {
        auto* self = static_cast<openssl_tls_handshake_adapter*>(arg);
        self->alert_received_ = true;
        self->alert_code_ = alert_code;
        self->set_failure("OpenSSL QUIC TLS alert received");
        return 1;
    }

    template <typename Callback>
    [[nodiscard]] static void (*dispatch_callback(Callback callback) noexcept)(void) {
        return reinterpret_cast<void (*)(void)>(callback);
    }

    // Build the OSSL_DISPATCH table for SSL_set_quic_tls_cbs
    static const OSSL_DISPATCH* quic_tls_dispatch_table() noexcept {
        static const OSSL_DISPATCH table[] = {
            {OSSL_FUNC_SSL_QUIC_TLS_CRYPTO_SEND, dispatch_callback(crypto_send_cb)},
            {OSSL_FUNC_SSL_QUIC_TLS_CRYPTO_RECV_RCD, dispatch_callback(crypto_recv_rcd_cb)},
            {OSSL_FUNC_SSL_QUIC_TLS_CRYPTO_RELEASE_RCD, dispatch_callback(crypto_release_rcd_cb)},
            {OSSL_FUNC_SSL_QUIC_TLS_YIELD_SECRET, dispatch_callback(yield_secret_cb)},
            {OSSL_FUNC_SSL_QUIC_TLS_GOT_TRANSPORT_PARAMS, dispatch_callback(got_transport_params_cb)},
            {OSSL_FUNC_SSL_QUIC_TLS_ALERT, dispatch_callback(alert_cb)},
            OSSL_DISPATCH_END
        };
        return table;
    }

    // Map OpenSSL protection level to FlowQ encryption level
    [[nodiscard]] static std::optional<tls_encryption_level> map_protection_level(uint32_t prot_level) noexcept {
        // OSSL_RECORD_PROTECTION_LEVEL_* constants
        switch (prot_level) {
        case OSSL_RECORD_PROTECTION_LEVEL_NONE:
            return tls_encryption_level::initial;
        case OSSL_RECORD_PROTECTION_LEVEL_HANDSHAKE:
            return tls_encryption_level::handshake;
        case OSSL_RECORD_PROTECTION_LEVEL_APPLICATION:
            return tls_encryption_level::application;
        default: return std::nullopt;
        }
    }

    // Update current TX level
    void update_tx_level(tls_encryption_level level) noexcept {
        current_tx_level_ = level;
    }

    void set_failure(std::string_view message) {
        state_ = handshake_state::failed;
        last_error_ = flowq::error{flowq::error_code::tls_error, std::string{message}};
    }

    [[nodiscard]] std::vector<std::byte>& secret_for(tls_encryption_level level, int direction) noexcept {
        const auto is_tx = direction == 1;
        switch (level) {
        case tls_encryption_level::initial:
            return is_tx ? initial_tx_secret_ : initial_rx_secret_;
        case tls_encryption_level::handshake:
            return is_tx ? handshake_tx_secret_ : handshake_rx_secret_;
        case tls_encryption_level::application:
            return is_tx ? application_tx_secret_ : application_rx_secret_;
        }
        return initial_rx_secret_;
    }

    [[nodiscard]] bool level_key_schedule_owned(tls_encryption_level level) const noexcept {
        switch (level) {
        case tls_encryption_level::initial:
            return !initial_rx_secret_.empty() && !initial_tx_secret_.empty();
        case tls_encryption_level::handshake:
            return !handshake_rx_secret_.empty() && !handshake_tx_secret_.empty();
        case tls_encryption_level::application:
            return !application_rx_secret_.empty() && !application_tx_secret_.empty();
        }
        return false;
    }

    [[nodiscard]] cipher_suite negotiated_cipher_suite() const noexcept {
        if (ssl_ == nullptr) {
            return cipher_suite::unknown;
        }
        const auto* current_cipher = SSL_get_current_cipher(ssl_);
        if (current_cipher == nullptr) {
            return cipher_suite::unknown;
        }
        switch (SSL_CIPHER_get_id(current_cipher)) {
        case TLS1_3_CK_AES_128_GCM_SHA256:
            return cipher_suite::aes_128_gcm_sha256;
        case TLS1_3_CK_AES_256_GCM_SHA384:
            return cipher_suite::aes_256_gcm_sha384;
        case TLS1_3_CK_CHACHA20_POLY1305_SHA256:
            return cipher_suite::chacha20_poly1305_sha256;
        default:
            return cipher_suite::unknown;
        }
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
            unsigned long openssl_err = ERR_get_error();
            char err_buf[256]{};
            ERR_error_string_n(openssl_err, err_buf, sizeof(err_buf));
            set_failure(err_buf);
            return last_error_;
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

    // TLS traffic secrets captured from OpenSSL callbacks.
    std::vector<std::byte> initial_rx_secret_;
    std::vector<std::byte> initial_tx_secret_;
    std::vector<std::byte> handshake_rx_secret_;
    std::vector<std::byte> handshake_tx_secret_;
    std::vector<std::byte> application_rx_secret_;
    std::vector<std::byte> application_tx_secret_;

    // Transport parameters passed to OpenSSL must outlive the SSL object.
    flowq::buffer local_transport_params_;
    std::vector<std::byte> peer_transport_params_;

    // Alert state
    bool alert_received_{};
    uint8_t alert_code_{};
    flowq::error last_error_{};

    // Config
    openssl_tls_config config_;
};

#else  // !FLOWQ_ENABLE_OPENSSL_QUIC_TLS

// Fail-closed adapter used when OpenSSL QUIC TLS is disabled at build time.
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
    [[nodiscard]] flowq::error advance() override {
        return flowq::error{flowq::error_code::tls_error, "OpenSSL QUIC TLS backend is disabled"};
    }
    [[nodiscard]] const std::vector<std::byte>& peer_transport_params() const noexcept {
        static const std::vector<std::byte> empty;
        return empty;
    }
};

#endif  // FLOWQ_ENABLE_OPENSSL_QUIC_TLS

} // namespace flowq::quic
