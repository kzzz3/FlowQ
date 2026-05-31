#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>
#include <flowq/quic/crypto_provider.hpp>
#include <flowq/quic/initial_keys.hpp>
#include <flowq/quic/key_derivation.hpp>
#include <flowq/quic/packet_pipeline.hpp>

#include <cstddef>
#include <span>
#include <utility>

namespace flowq::quic {

struct aead_protector_result {
    flowq::error error{};
    bool ok() const noexcept { return error.ok(); }
};

class openssl_aead_packet_protector final : public packet_protector {
public:
    /// Create an AEAD packet protector from pre-derived key material.
    /// Returns an error result if the material is invalid.
    /// On success, the protector is ready for use and the returned error is default (ok).
    [[nodiscard]] static aead_protector_result create(
        openssl_aead_packet_protector& output,
        protection_level level,
        traffic_key_material material) {
#if !defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)
        (void)output;
        (void)level;
        (void)material;
        return {not_ready_error()};
#else
        if (!material.ok()) {
            return {material.error};
        }
        if (material.suite == cipher_suite::unknown) {
            return {flowq::error{flowq::error_code::tls_error, "cannot create AEAD protector with unknown cipher suite"}};
        }
        // Support AES-128-GCM, AES-256-GCM, and ChaCha20-Poly1305
        if (material.suite != cipher_suite::aes_128_gcm_sha256 &&
            material.suite != cipher_suite::aes_256_gcm_sha384 &&
            material.suite != cipher_suite::chacha20_poly1305_sha256) {
            return {flowq::error{flowq::error_code::tls_error, "unsupported cipher suite for openssl_aead_packet_protector"}};
        }
        const auto lengths = cipher_suite_key_lengths(material.suite);
        if (material.key.size() != lengths.key ||
            material.iv.size() != lengths.iv ||
            material.header_protection_key.size() != lengths.header_protection) {
            return {flowq::error{flowq::error_code::tls_error, "traffic key material length mismatch for cipher suite"}};
        }
        output = openssl_aead_packet_protector{level, std::move(material)};
        return {};
#endif
    }

    /// Default constructor creates an invalid protector.
    openssl_aead_packet_protector() = default;

    openssl_aead_packet_protector(const openssl_aead_packet_protector&) = delete;
    openssl_aead_packet_protector& operator=(const openssl_aead_packet_protector&) = delete;
    openssl_aead_packet_protector(openssl_aead_packet_protector&&) noexcept = default;
    openssl_aead_packet_protector& operator=(openssl_aead_packet_protector&&) noexcept = default;

    [[nodiscard]] bool is_ready() const noexcept {
        return ready_;
    }

    [[nodiscard]] protection_level level() const noexcept override {
        return level_;
    }

    [[nodiscard]] packet_security_level security_level() const noexcept override {
        return packet_security_level::authenticated_encrypted;
    }

    [[nodiscard]] crypto_provider_status provider_status() const noexcept override {
#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)
        if (!ready_) {
            return crypto_provider_status::unavailable();
        }
        return crypto_provider_status::available(
            material_.suite,
            crypto_capabilities{true, true, true, true, false});
#else
        return crypto_provider_status::unavailable();
#endif
    }

    [[nodiscard]] std::size_t protection_overhead() const noexcept override {
        // All supported AEAD suites (AES-128-GCM, AES-256-GCM, ChaCha20-Poly1305)
        // use a 16-byte authentication tag
        return 16;
    }

    [[nodiscard]] bool header_protection_enabled() const noexcept override {
#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)
        return ready_;
#else
        return false;
#endif
    }

    [[nodiscard]] header_protection_mask_result header_protection_mask(std::span<const std::byte> sample) const override {
#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)
        if (!ready_) {
            return {{}, not_ready_error()};
        }
        return flowq::quic::header_protection_mask(
            material_.suite,
            std::span<const std::byte>{material_.header_protection_key.data(), material_.header_protection_key.size()},
            sample);
#else
        (void)sample;
        return {{}, not_ready_error()};
#endif
    }

    [[nodiscard]] packet_protection_result protect(std::span<const std::byte> plaintext) const override {
        (void)plaintext;
        return {{}, flowq::error{flowq::error_code::protocol_error, "AEAD packet protection requires packet context with packet number"}};
    }

    [[nodiscard]] packet_protection_result unprotect(std::span<const std::byte> protected_payload) const override {
        (void)protected_payload;
        return {{}, flowq::error{flowq::error_code::protocol_error, "AEAD packet protection requires packet context with packet number"}};
    }

    [[nodiscard]] packet_protection_result protect(
        const packet_protection_context& context,
        std::span<const std::byte> plaintext) const override {
#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)
        if (!ready_) {
            return {{}, not_ready_error()};
        }
        if (context.associated_data.empty()) {
            return {{}, flowq::error{flowq::error_code::protocol_error, "AEAD protection requires associated data"}};
        }
        auto sealed = aead_seal(
            material_.suite,
            std::span<const std::byte>{material_.key.data(), material_.key.size()},
            std::span<const std::byte>{material_.iv.data(), material_.iv.size()},
            context.number.value,
            context.associated_data,
            plaintext);
        return {std::move(sealed.payload), sealed.error};
#else
        (void)context;
        (void)plaintext;
        return {{}, not_ready_error()};
#endif
    }

    [[nodiscard]] packet_protection_result unprotect(
        const packet_protection_context& context,
        std::span<const std::byte> protected_payload) const override {
#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)
        if (!ready_) {
            return {{}, not_ready_error()};
        }
        if (context.associated_data.empty()) {
            return {{}, flowq::error{flowq::error_code::protocol_error, "AEAD unprotection requires associated data"}};
        }
        auto opened = aead_open(
            material_.suite,
            std::span<const std::byte>{material_.key.data(), material_.key.size()},
            std::span<const std::byte>{material_.iv.data(), material_.iv.size()},
            context.number.value,
            context.associated_data,
            protected_payload);
        return {std::move(opened.payload), opened.error};
#else
        (void)context;
        (void)protected_payload;
        return {{}, not_ready_error()};
#endif
    }

private:
    openssl_aead_packet_protector(protection_level level, traffic_key_material material)
        : level_{level}, material_{std::move(material)} {
#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)
        ready_ = true;
#endif
    }

    [[nodiscard]] static flowq::error not_ready_error() {
        return flowq::error{flowq::error_code::tls_error, "OpenSSL AEAD packet protector is not initialized or crypto backend is disabled"};
    }

    protection_level level_{};
    traffic_key_material material_{};
    bool ready_{false};
};

} // namespace flowq::quic
