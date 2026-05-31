#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>
#include <flowq/quic/crypto_provider.hpp>
#include <flowq/quic/initial_keys.hpp>
#include <flowq/secure.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace flowq::quic {

struct traffic_key_material {
    flowq::buffer key;
    flowq::buffer iv;
    flowq::buffer header_protection_key;
    cipher_suite suite{cipher_suite::unknown};
    flowq::error error{};

    /// Default constructor.
    traffic_key_material() = default;

    /// Parameterized constructor.
    traffic_key_material(flowq::buffer k, flowq::buffer i, flowq::buffer hp,
                        cipher_suite s, flowq::error e)
        : key{std::move(k)}, iv{std::move(i)}, header_protection_key{std::move(hp)},
          suite{s}, error{std::move(e)} {}

    /// Destructor securely zeroes all key material.
    ~traffic_key_material() {
        secure_zero_buffer(key);
        secure_zero_buffer(iv);
        secure_zero_buffer(header_protection_key);
    }

    // Move semantics
    traffic_key_material(traffic_key_material&& other) noexcept
        : key{std::move(other.key)},
          iv{std::move(other.iv)},
          header_protection_key{std::move(other.header_protection_key)},
          suite{other.suite},
          error{std::move(other.error)} {
        // Other's buffers are now empty after move
    }

    traffic_key_material& operator=(traffic_key_material&& other) noexcept {
        if (this != &other) {
            // Securely zero current key material before moving
            secure_zero_buffer(key);
            secure_zero_buffer(iv);
            secure_zero_buffer(header_protection_key);

            key = std::move(other.key);
            iv = std::move(other.iv);
            header_protection_key = std::move(other.header_protection_key);
            suite = other.suite;
            error = std::move(other.error);
        }
        return *this;
    }

    // Delete copy semantics for security
    traffic_key_material(const traffic_key_material&) = delete;
    traffic_key_material& operator=(const traffic_key_material&) = delete;

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

[[nodiscard]] inline flowq::error key_derivation_error(const char* message) {
    return flowq::error{flowq::error_code::tls_error, message};
}

/// Derive traffic key material from a TLS exporter secret using HKDF-Expand-Label (RFC 9001 Section 5).
/// The secret is expected to be the output of a TLS 1.3 key schedule exporter (e.g., client_handshake_traffic_secret).
[[nodiscard]] inline traffic_key_material derive_traffic_key_material(
    std::span<const std::byte> secret,
    cipher_suite suite) {
#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)
    const auto lengths = cipher_suite_key_lengths(suite);
    if (lengths.key == 0) {
        return {{}, {}, {}, suite, key_derivation_error("unsupported cipher suite for traffic key derivation")};
    }
    if (secret.empty()) {
        return {{}, {}, {}, suite, key_derivation_error("empty secret for traffic key derivation")};
    }

    auto key_result = detail::hkdf_expand_label_sha256(secret, "quic key", lengths.key);
    if (!key_result.ok()) {
        return {{}, {}, {}, suite, key_result.error};
    }

    auto iv_result = detail::hkdf_expand_label_sha256(secret, "quic iv", lengths.iv);
    if (!iv_result.ok()) {
        return {{}, {}, {}, suite, iv_result.error};
    }

    auto hp_result = detail::hkdf_expand_label_sha256(secret, "quic hp", lengths.header_protection);
    if (!hp_result.ok()) {
        return {{}, {}, {}, suite, hp_result.error};
    }

    return {std::move(key_result.payload), std::move(iv_result.payload), std::move(hp_result.payload), suite, {}};
#else
    (void)secret;
    (void)suite;
    return {{}, {}, {}, suite, key_derivation_error("OpenSSL crypto backend is disabled")};
#endif
}

[[nodiscard]] inline traffic_key_material derive_traffic_key_material(
    const flowq::buffer& secret,
    cipher_suite suite) {
    return derive_traffic_key_material(std::span<const std::byte>{secret.data(), secret.size()}, suite);
}

} // namespace flowq::quic
