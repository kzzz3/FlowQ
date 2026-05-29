#include <flowq/quic/key_derivation.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <initializer_list>
#include <vector>

namespace {

std::vector<std::byte> bytes(std::initializer_list<unsigned int> values) {
    std::vector<std::byte> output;
    output.reserve(values.size());
    for (auto value : values) {
        output.push_back(static_cast<std::byte>(value & 0xffU));
    }
    return output;
}

void check_buffer(const flowq::buffer& actual, std::initializer_list<unsigned int> expected) {
    REQUIRE(actual.size() == expected.size());
    std::size_t index = 0;
    for (auto value : expected) {
        CHECK(actual.data()[index] == static_cast<std::byte>(value & 0xffU));
        ++index;
    }
}

} // namespace

TEST_CASE("cipher_suite_key_lengths returns correct lengths for all supported suites") {
    auto aes128 = flowq::quic::cipher_suite_key_lengths(flowq::quic::cipher_suite::aes_128_gcm_sha256);
    CHECK(aes128.key == 16);
    CHECK(aes128.iv == 12);
    CHECK(aes128.header_protection == 16);

    auto aes256 = flowq::quic::cipher_suite_key_lengths(flowq::quic::cipher_suite::aes_256_gcm_sha384);
    CHECK(aes256.key == 32);
    CHECK(aes256.iv == 12);
    CHECK(aes256.header_protection == 32);

    auto chacha = flowq::quic::cipher_suite_key_lengths(flowq::quic::cipher_suite::chacha20_poly1305_sha256);
    CHECK(chacha.key == 32);
    CHECK(chacha.iv == 12);
    CHECK(chacha.header_protection == 32);

    auto unknown = flowq::quic::cipher_suite_key_lengths(flowq::quic::cipher_suite::unknown);
    CHECK(unknown.key == 0);
    CHECK(unknown.iv == 0);
    CHECK(unknown.header_protection == 0);
}

TEST_CASE("derive_traffic_key_material rejects unknown cipher suite") {
    auto result = flowq::quic::derive_traffic_key_material(
        bytes({0x01, 0x02, 0x03}),
        flowq::quic::cipher_suite::unknown);
    CHECK_FALSE(result.ok());
    CHECK(result.suite == flowq::quic::cipher_suite::unknown);
}

TEST_CASE("derive_traffic_key_material rejects empty secret") {
    auto result = flowq::quic::derive_traffic_key_material(
        std::span<const std::byte>{},
        flowq::quic::cipher_suite::aes_128_gcm_sha256);
    CHECK_FALSE(result.ok());
}

#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)

TEST_CASE("derive_traffic_key_material derives AES-128-GCM keys from known secret") {
    // Use the RFC 9001 client initial secret as input for derivation
    auto secrets = flowq::quic::derive_initial_secrets(bytes({0x83, 0x94, 0xc8, 0xf0, 0x3e, 0x51, 0x57, 0x08}));
    REQUIRE(secrets.ok());

    auto material = flowq::quic::derive_traffic_key_material(
        secrets.client_initial_secret,
        flowq::quic::cipher_suite::aes_128_gcm_sha256);
    REQUIRE(material.ok());
    CHECK(material.suite == flowq::quic::cipher_suite::aes_128_gcm_sha256);
    CHECK(material.key.size() == 16);
    CHECK(material.iv.size() == 12);
    CHECK(material.header_protection_key.size() == 16);
}

TEST_CASE("derive_traffic_key_material derives consistent keys for same input") {
    auto secrets = flowq::quic::derive_initial_secrets(bytes({0x83, 0x94, 0xc8, 0xf0, 0x3e, 0x51, 0x57, 0x08}));
    REQUIRE(secrets.ok());

    auto m1 = flowq::quic::derive_traffic_key_material(
        secrets.server_initial_secret,
        flowq::quic::cipher_suite::aes_128_gcm_sha256);
    auto m2 = flowq::quic::derive_traffic_key_material(
        secrets.server_initial_secret,
        flowq::quic::cipher_suite::aes_128_gcm_sha256);
    REQUIRE(m1.ok());
    REQUIRE(m2.ok());

    CHECK(m1.key.size() == m2.key.size());
    CHECK(m1.iv.size() == m2.iv.size());
    CHECK(m1.header_protection_key.size() == m2.header_protection_key.size());

    for (std::size_t i = 0; i < m1.key.size(); ++i) {
        CHECK(m1.key.data()[i] == m2.key.data()[i]);
    }
    for (std::size_t i = 0; i < m1.iv.size(); ++i) {
        CHECK(m1.iv.data()[i] == m2.iv.data()[i]);
    }
}

TEST_CASE("derive_traffic_key_material produces different keys for different secrets") {
    auto secrets = flowq::quic::derive_initial_secrets(bytes({0x83, 0x94, 0xc8, 0xf0, 0x3e, 0x51, 0x57, 0x08}));
    REQUIRE(secrets.ok());

    auto client_material = flowq::quic::derive_traffic_key_material(
        secrets.client_initial_secret,
        flowq::quic::cipher_suite::aes_128_gcm_sha256);
    auto server_material = flowq::quic::derive_traffic_key_material(
        secrets.server_initial_secret,
        flowq::quic::cipher_suite::aes_128_gcm_sha256);
    REQUIRE(client_material.ok());
    REQUIRE(server_material.ok());

    bool keys_differ = false;
    for (std::size_t i = 0; i < client_material.key.size(); ++i) {
        if (client_material.key.data()[i] != server_material.key.data()[i]) {
            keys_differ = true;
            break;
        }
    }
    CHECK(keys_differ);
}

#else

TEST_CASE("derive_traffic_key_material reports disabled when OpenSSL is off") {
    auto result = flowq::quic::derive_traffic_key_material(
        bytes({0x01, 0x02, 0x03}),
        flowq::quic::cipher_suite::aes_128_gcm_sha256);
    CHECK_FALSE(result.ok());
}

#endif
