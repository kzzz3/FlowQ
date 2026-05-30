#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>

#include <array>
#include <cstddef>
#include <string_view>

namespace flowq::quic {

enum class cipher_suite {
    unknown,
    aes_128_gcm_sha256,
    aes_256_gcm_sha384,
    chacha20_poly1305_sha256
};

[[nodiscard]] inline std::string_view cipher_suite_name(cipher_suite suite) noexcept {
    switch (suite) {
    case cipher_suite::aes_128_gcm_sha256:
        return "TLS_AES_128_GCM_SHA256";
    case cipher_suite::aes_256_gcm_sha384:
        return "TLS_AES_256_GCM_SHA384";
    case cipher_suite::chacha20_poly1305_sha256:
        return "TLS_CHACHA20_POLY1305_SHA256";
    case cipher_suite::unknown:
        return "unknown";
    }
    return "unknown";
}

struct crypto_capabilities {
    bool hkdf{};
    bool aead_seal{};
    bool aead_open{};
    bool header_protection{};
    bool tls_owns_key_schedule{};
};

[[nodiscard]] inline bool production_crypto_capable(const crypto_capabilities& capabilities) noexcept {
    return capabilities.hkdf &&
        capabilities.aead_seal &&
        capabilities.aead_open &&
        capabilities.header_protection &&
        capabilities.tls_owns_key_schedule;
}

struct crypto_provider_status {
    bool available_from_external_provider{};
    cipher_suite suite{cipher_suite::unknown};
    crypto_capabilities capabilities{};

    [[nodiscard]] static crypto_provider_status unavailable() noexcept {
        return {};
    }

    [[nodiscard]] static crypto_provider_status available(cipher_suite selected_suite, crypto_capabilities supported_capabilities) noexcept {
        return {true, selected_suite, supported_capabilities};
    }

    [[nodiscard]] bool production_ready() const noexcept {
        return available_from_external_provider && suite != cipher_suite::unknown && production_crypto_capable(capabilities);
    }

    [[nodiscard]] bool key_schedule_ready() const noexcept {
        return available_from_external_provider && suite != cipher_suite::unknown && capabilities.tls_owns_key_schedule;
    }

    [[nodiscard]] bool packet_protection_ready() const noexcept {
        return available_from_external_provider &&
            suite != cipher_suite::unknown &&
            capabilities.hkdf &&
            capabilities.aead_seal &&
            capabilities.aead_open &&
            capabilities.header_protection;
    }
};

struct crypto_bytes_result {
    flowq::buffer payload;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

struct header_protection_mask_result {
    std::array<std::byte, 5> mask{};
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

} // namespace flowq::quic
