#pragma once

#include <flowq/error.hpp>
#include <flowq/quic/crypto_provider.hpp>

#include <string_view>
#include <vector>

#if defined(FLOWQ_ENABLE_OPENSSL_QUIC_TLS)
#include <openssl/opensslv.h>
#include <openssl/ssl.h>
#endif

namespace flowq::quic {

enum class tls_provider_family {
    unavailable,
    openssl_quic_tls
};

struct tls_provider_metadata {
    std::string_view name;
    std::string_view version;
    tls_provider_family family{tls_provider_family::unavailable};
    bool quic_tls_api_available{};
    std::vector<cipher_suite> cipher_suites;
};

struct tls_provider_backend_status {
    bool available{};
    tls_provider_metadata metadata;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return available && error.ok();
    }
};

[[nodiscard]] inline flowq::error tls_provider_backend_error(const char* message) {
    return flowq::error{flowq::error_code::tls_error, message};
}

[[nodiscard]] inline tls_provider_backend_status disabled_tls_provider_backend_status() {
    return {
        false,
        tls_provider_metadata{"disabled", {}, tls_provider_family::unavailable, false, {}},
        tls_provider_backend_error("TLS provider backend is disabled")
    };
}

[[nodiscard]] inline tls_provider_backend_status openssl_quic_tls_backend_status() {
#if defined(FLOWQ_ENABLE_OPENSSL_QUIC_TLS)
    return {
        true,
        tls_provider_metadata{
            "OpenSSL QUIC TLS",
            OpenSSL_version(OPENSSL_VERSION),
            tls_provider_family::openssl_quic_tls,
            true,
            {
                cipher_suite::aes_128_gcm_sha256
            }
        },
        {}
    };
#else
    return {
        false,
        tls_provider_metadata{"OpenSSL QUIC TLS", {}, tls_provider_family::openssl_quic_tls, false, {}},
        tls_provider_backend_error("OpenSSL QUIC TLS backend is disabled")
    };
#endif
}

} // namespace flowq::quic
