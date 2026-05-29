#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>
#include <flowq/quic/crypto_provider.hpp>
#include <flowq/quic/initial_keys.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace flowq::quic {

struct cipher_key_lengths {
    std::size_t key{};
    std::size_t iv{};
    std::size_t header_protection{};
};

[[nodiscard]] inline cipher_key_lengths cipher_suite_key_lengths(cipher_suite suite) {
    switch (suite) {
    case cipher_suite::aes_128_gcm_sha256:
        return {16, 12, 16};
    case cipher_suite::aes_256_gcm_sha384:
        return {32, 12, 32};
    case cipher_suite::chacha20_poly1305_sha256:
        return {32, 12, 32};
    default:
        return {};
    }
}

struct traffic_key_material {
    flowq::buffer key;
    flowq::buffer iv;
    flowq::buffer header_protection_key;
    cipher_suite suite{cipher_suite::unknown};
    flowq::error error{};

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
