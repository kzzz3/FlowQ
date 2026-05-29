#include <flowq/quic/tls_provider_backend.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

TEST_CASE("OpenSSL QUIC TLS backend reports status from build configuration") {
    auto status = flowq::quic::openssl_quic_tls_backend_status();

#if !defined(FLOWQ_ENABLE_OPENSSL_QUIC_TLS)
    CHECK_FALSE(status.available);
    CHECK_FALSE(status.ok());
    CHECK_FALSE(status.metadata.quic_tls_api_available);
    CHECK(status.metadata.family == flowq::quic::tls_provider_family::openssl_quic_tls);
    CHECK(status.metadata.name == std::string_view{"OpenSSL QUIC TLS"});
    CHECK(status.metadata.cipher_suites.empty());
#else
    CHECK(status.available);
    CHECK(status.ok());
    CHECK(status.metadata.quic_tls_api_available);
    CHECK(status.metadata.family == flowq::quic::tls_provider_family::openssl_quic_tls);
    CHECK(status.metadata.name == std::string_view{"OpenSSL QUIC TLS"});
    CHECK_FALSE(status.metadata.version.empty());
    REQUIRE(status.metadata.cipher_suites.size() == 3);
    CHECK(status.metadata.cipher_suites[0] == flowq::quic::cipher_suite::aes_128_gcm_sha256);
    CHECK(status.metadata.cipher_suites[1] == flowq::quic::cipher_suite::aes_256_gcm_sha384);
    CHECK(status.metadata.cipher_suites[2] == flowq::quic::cipher_suite::chacha20_poly1305_sha256);
#endif
}

TEST_CASE("OpenSSL QUIC TLS backend does not expose a fake handshake adapter") {
    auto status = flowq::quic::openssl_quic_tls_backend_status();

#if !defined(FLOWQ_ENABLE_OPENSSL_QUIC_TLS)
    CHECK_FALSE(status.ok());
#else
    CHECK(status.ok());
    CHECK(status.metadata.quic_tls_api_available);
#endif
    CHECK(flowq::quic::disabled_tls_provider_backend_status().metadata.family == flowq::quic::tls_provider_family::unavailable);
    CHECK_FALSE(flowq::quic::disabled_tls_provider_backend_status().ok());
}
