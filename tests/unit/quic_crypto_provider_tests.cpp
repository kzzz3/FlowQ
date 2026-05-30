#include <flowq/quic/crypto_provider.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <string_view>

TEST_CASE("cipher suite names use TLS registry identifiers") {
    CHECK(std::string_view{flowq::quic::cipher_suite_name(flowq::quic::cipher_suite::unknown)} == "unknown");
    CHECK(std::string_view{flowq::quic::cipher_suite_name(flowq::quic::cipher_suite::aes_128_gcm_sha256)} == "TLS_AES_128_GCM_SHA256");
    CHECK(std::string_view{flowq::quic::cipher_suite_name(flowq::quic::cipher_suite::aes_256_gcm_sha384)} == "TLS_AES_256_GCM_SHA384");
    CHECK(std::string_view{flowq::quic::cipher_suite_name(flowq::quic::cipher_suite::chacha20_poly1305_sha256)} == "TLS_CHACHA20_POLY1305_SHA256");
}

TEST_CASE("crypto provider capabilities require every production primitive") {
    flowq::quic::crypto_capabilities capabilities{};

    CHECK_FALSE(flowq::quic::production_crypto_capable(capabilities));

    capabilities.hkdf = true;
    capabilities.aead_seal = true;
    capabilities.aead_open = true;
    capabilities.header_protection = true;
    capabilities.tls_owns_key_schedule = true;

    CHECK(flowq::quic::production_crypto_capable(capabilities));
}

TEST_CASE("crypto provider status is unavailable until backed by external TLS and packet protection") {
    const auto unavailable = flowq::quic::crypto_provider_status::unavailable();
    CHECK_FALSE(unavailable.packet_protection_ready());
    CHECK_FALSE(unavailable.production_ready());
    CHECK(unavailable.suite == flowq::quic::cipher_suite::unknown);

    const auto packet_ready = flowq::quic::crypto_provider_status::available(
        flowq::quic::cipher_suite::aes_128_gcm_sha256,
        flowq::quic::crypto_capabilities{
            true,
            true,
            true,
            true,
            false
        });

    CHECK(packet_ready.packet_protection_ready());
    CHECK_FALSE(packet_ready.production_ready());

    const auto ready = flowq::quic::crypto_provider_status::available(
        flowq::quic::cipher_suite::aes_128_gcm_sha256,
        flowq::quic::crypto_capabilities{
            true,
            true,
            true,
            true,
            true
        });

    CHECK(ready.packet_protection_ready());
    CHECK(ready.production_ready());
    CHECK(ready.suite == flowq::quic::cipher_suite::aes_128_gcm_sha256);
}

TEST_CASE("crypto provider result structs report ok through flowq errors") {
    flowq::quic::crypto_bytes_result bytes{};
    CHECK(bytes.ok());

    flowq::quic::header_protection_mask_result mask{};
    CHECK(mask.ok());
    CHECK(mask.mask == std::array<std::byte, 5>{});
}
