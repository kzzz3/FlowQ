#include <flowq/quic/key_derivation.hpp>
#include <flowq/quic/initial_keys.hpp>
#include <flowq/quic/openssl_aead_protector.hpp>
#include <flowq/quic/packet_pipeline.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <initializer_list>
#include <span>
#include <variant>
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

flowq::quic::connection_id cid(std::initializer_list<unsigned int> values) {
    return flowq::quic::connection_id{flowq::buffer{bytes(values)}};
}

#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)

flowq::quic::traffic_key_material make_test_material() {
    auto secrets = flowq::quic::derive_initial_secrets(bytes({0x83, 0x94, 0xc8, 0xf0, 0x3e, 0x51, 0x57, 0x08}));
    return flowq::quic::derive_traffic_key_material(
        secrets.client_initial_secret,
        flowq::quic::cipher_suite::aes_128_gcm_sha256);
}

flowq::quic::traffic_key_material make_server_test_material() {
    auto secrets = flowq::quic::derive_initial_secrets(bytes({0x83, 0x94, 0xc8, 0xf0, 0x3e, 0x51, 0x57, 0x08}));
    return flowq::quic::derive_traffic_key_material(
        secrets.server_initial_secret,
        flowq::quic::cipher_suite::aes_128_gcm_sha256);
}

#endif

} // namespace

TEST_CASE("openssl_aead_packet_protector creation fails with invalid material") {
    flowq::quic::openssl_aead_packet_protector protector;
    auto material = flowq::quic::traffic_key_material{};
    material.error = flowq::error{flowq::error_code::tls_error, "test error"};
    auto result = flowq::quic::openssl_aead_packet_protector::create(
        protector,
        flowq::quic::protection_level::handshake,
        std::move(material));
    CHECK_FALSE(result.ok());
    CHECK_FALSE(protector.is_ready());
}

#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)

TEST_CASE("openssl_aead_packet_protector creation succeeds with valid key material") {
    auto material = make_test_material();
    REQUIRE(material.ok());
    flowq::quic::openssl_aead_packet_protector protector;
    auto result = flowq::quic::openssl_aead_packet_protector::create(
        protector,
        flowq::quic::protection_level::handshake,
        std::move(material));
    REQUIRE(result.ok());
    CHECK(protector.is_ready());
    CHECK(protector.level() == flowq::quic::protection_level::handshake);
    CHECK(protector.security_level() == flowq::quic::packet_security_level::authenticated_encrypted);
}

TEST_CASE("openssl_aead_packet_protector reports correct provider status") {
    auto material = make_test_material();
    REQUIRE(material.ok());
    flowq::quic::openssl_aead_packet_protector protector;
    REQUIRE(flowq::quic::openssl_aead_packet_protector::create(
        protector,
        flowq::quic::protection_level::application,
        std::move(material)).ok());

    auto status = protector.provider_status();
    CHECK(status.available_from_external_provider);
    CHECK(status.suite == flowq::quic::cipher_suite::aes_128_gcm_sha256);
    CHECK(status.packet_protection_ready());
    CHECK_FALSE(status.production_ready()); // tls_owns_key_schedule is false
}

TEST_CASE("openssl_aead_packet_protector reports 16-byte overhead and header protection enabled") {
    auto material = make_test_material();
    REQUIRE(material.ok());
    flowq::quic::openssl_aead_packet_protector protector;
    REQUIRE(flowq::quic::openssl_aead_packet_protector::create(
        protector,
        flowq::quic::protection_level::handshake,
        std::move(material)).ok());

    CHECK(protector.protection_overhead() == 16);
    CHECK(protector.header_protection_enabled());
}

TEST_CASE("openssl_aead_packet_protector seal and open round trips plaintext") {
    auto material = make_test_material();
    REQUIRE(material.ok());
    flowq::quic::openssl_aead_packet_protector protector;
    REQUIRE(flowq::quic::openssl_aead_packet_protector::create(
        protector,
        flowq::quic::protection_level::handshake,
        std::move(material)).ok());

    auto plaintext = bytes({0x01, 0x02, 0x03, 0x04, 0x05});
    auto aad = bytes({0xc3, 0x00, 0x00, 0x00, 0x01});
    flowq::quic::packet_protection_context context{
        flowq::quic::packet_number{flowq::quic::packet_number_space::handshake, 1},
        std::span<const std::byte>{aad.data(), aad.size()}
    };

    auto sealed = protector.protect(context, plaintext);
    REQUIRE(sealed.ok());
    CHECK(sealed.payload.size() == plaintext.size() + 16); // plaintext + GCM tag

    auto opened = protector.unprotect(context, std::span<const std::byte>{sealed.payload.data(), sealed.payload.size()});
    REQUIRE(opened.ok());
    CHECK(opened.payload.size() == plaintext.size());
    for (std::size_t i = 0; i < plaintext.size(); ++i) {
        CHECK(opened.payload.data()[i] == static_cast<std::byte>(plaintext[i]));
    }
}

TEST_CASE("openssl_aead_packet_protector open rejects tampered ciphertext") {
    auto material = make_test_material();
    REQUIRE(material.ok());
    flowq::quic::openssl_aead_packet_protector protector;
    REQUIRE(flowq::quic::openssl_aead_packet_protector::create(
        protector,
        flowq::quic::protection_level::handshake,
        std::move(material)).ok());

    auto plaintext = bytes({0xde, 0xad, 0xbe, 0xef});
    auto aad = bytes({0xc3, 0x00, 0x00, 0x00, 0x01});
    flowq::quic::packet_protection_context context{
        flowq::quic::packet_number{flowq::quic::packet_number_space::handshake, 42},
        std::span<const std::byte>{aad.data(), aad.size()}
    };

    auto sealed = protector.protect(context, plaintext);
    REQUIRE(sealed.ok());

    // Tamper with the first ciphertext byte
    auto tampered = sealed.payload;
    tampered.data()[0] ^= std::byte{0xff};

    auto opened = protector.unprotect(context, std::span<const std::byte>{tampered.data(), tampered.size()});
    CHECK_FALSE(opened.ok());
    CHECK(opened.error.code() == flowq::error_code::tls_error);
}

TEST_CASE("openssl_aead_packet_protector open rejects tampered AAD") {
    auto material = make_test_material();
    REQUIRE(material.ok());
    flowq::quic::openssl_aead_packet_protector protector;
    REQUIRE(flowq::quic::openssl_aead_packet_protector::create(
        protector,
        flowq::quic::protection_level::handshake,
        std::move(material)).ok());

    auto plaintext = bytes({0xca, 0xfe});
    auto aad = bytes({0xc3, 0x00, 0x00, 0x00, 0x01});
    flowq::quic::packet_protection_context context{
        flowq::quic::packet_number{flowq::quic::packet_number_space::handshake, 5},
        std::span<const std::byte>{aad.data(), aad.size()}
    };

    auto sealed = protector.protect(context, plaintext);
    REQUIRE(sealed.ok());

    // Use different AAD for open
    auto tampered_aad = aad;
    tampered_aad[0] ^= std::byte{0x01};
    flowq::quic::packet_protection_context wrong_aad_context{
        flowq::quic::packet_number{flowq::quic::packet_number_space::handshake, 5},
        std::span<const std::byte>{tampered_aad.data(), tampered_aad.size()}
    };

    auto opened = protector.unprotect(wrong_aad_context, std::span<const std::byte>{sealed.payload.data(), sealed.payload.size()});
    CHECK_FALSE(opened.ok());
}

TEST_CASE("openssl_aead_packet_protector open rejects wrong key") {
    auto material1 = make_test_material();
    auto material2 = make_server_test_material();
    REQUIRE(material1.ok());
    REQUIRE(material2.ok());

    flowq::quic::openssl_aead_packet_protector protector1;
    flowq::quic::openssl_aead_packet_protector protector2;
    REQUIRE(flowq::quic::openssl_aead_packet_protector::create(
        protector1,
        flowq::quic::protection_level::handshake,
        std::move(material1)).ok());
    REQUIRE(flowq::quic::openssl_aead_packet_protector::create(
        protector2,
        flowq::quic::protection_level::handshake,
        std::move(material2)).ok());

    auto plaintext = bytes({0x01, 0x02, 0x03});
    auto aad = bytes({0xc3, 0x00, 0x00, 0x00, 0x01});
    flowq::quic::packet_protection_context context{
        flowq::quic::packet_number{flowq::quic::packet_number_space::handshake, 1},
        std::span<const std::byte>{aad.data(), aad.size()}
    };

    auto sealed = protector1.protect(context, plaintext);
    REQUIRE(sealed.ok());

    // Try to open with different key
    auto opened = protector2.unprotect(context, std::span<const std::byte>{sealed.payload.data(), sealed.payload.size()});
    CHECK_FALSE(opened.ok());
}

TEST_CASE("openssl_aead_packet_protector open rejects wrong packet number") {
    auto material = make_test_material();
    REQUIRE(material.ok());
    flowq::quic::openssl_aead_packet_protector protector;
    REQUIRE(flowq::quic::openssl_aead_packet_protector::create(
        protector,
        flowq::quic::protection_level::handshake,
        std::move(material)).ok());

    auto plaintext = bytes({0xaa, 0xbb});
    auto aad = bytes({0xc3, 0x00, 0x00, 0x00, 0x01});
    flowq::quic::packet_protection_context seal_context{
        flowq::quic::packet_number{flowq::quic::packet_number_space::handshake, 10},
        std::span<const std::byte>{aad.data(), aad.size()}
    };

    auto sealed = protector.protect(seal_context, plaintext);
    REQUIRE(sealed.ok());

    // Open with wrong packet number (nonce mismatch → authentication failure)
    flowq::quic::packet_protection_context open_context{
        flowq::quic::packet_number{flowq::quic::packet_number_space::handshake, 11},
        std::span<const std::byte>{aad.data(), aad.size()}
    };
    auto opened = protector.unprotect(open_context, std::span<const std::byte>{sealed.payload.data(), sealed.payload.size()});
    CHECK_FALSE(opened.ok());
}

TEST_CASE("openssl_aead_packet_protector context-free protect and unprotect return errors") {
    auto material = make_test_material();
    REQUIRE(material.ok());
    flowq::quic::openssl_aead_packet_protector protector;
    REQUIRE(flowq::quic::openssl_aead_packet_protector::create(
        protector,
        flowq::quic::protection_level::handshake,
        std::move(material)).ok());

    auto plaintext = bytes({0x01});
    CHECK_FALSE(protector.protect(plaintext).ok());
    CHECK_FALSE(protector.unprotect(plaintext).ok());
}

TEST_CASE("openssl_aead_packet_protector header protection mask produces valid mask") {
    auto material = make_test_material();
    REQUIRE(material.ok());
    flowq::quic::openssl_aead_packet_protector protector;
    REQUIRE(flowq::quic::openssl_aead_packet_protector::create(
        protector,
        flowq::quic::protection_level::handshake,
        std::move(material)).ok());

    auto sample = bytes({0xd1, 0xb1, 0xc9, 0x8d, 0xd7, 0x68, 0x9f, 0xb8, 0xec, 0x11, 0xd2, 0x42, 0xb1, 0x23, 0xdc, 0x9b});
    auto mask = protector.header_protection_mask(std::span<const std::byte>{sample.data(), sample.size()});
    CHECK(mask.ok());
}

TEST_CASE("openssl_aead_packet_protector seals and opens minimal single-byte plaintext") {
    auto material = make_test_material();
    REQUIRE(material.ok());
    flowq::quic::openssl_aead_packet_protector protector;
    REQUIRE(flowq::quic::openssl_aead_packet_protector::create(
        protector,
        flowq::quic::protection_level::handshake,
        std::move(material)).ok());

    auto plaintext = bytes({0x42});
    auto aad = bytes({0xc3, 0x00, 0x00, 0x00, 0x01});
    flowq::quic::packet_protection_context context{
        flowq::quic::packet_number{flowq::quic::packet_number_space::handshake, 0},
        std::span<const std::byte>{aad.data(), aad.size()}
    };

    auto sealed = protector.protect(context, plaintext);
    REQUIRE(sealed.ok());
    CHECK(sealed.payload.size() == 1 + 16); // 1 byte plaintext + GCM tag

    auto opened = protector.unprotect(context, std::span<const std::byte>{sealed.payload.data(), sealed.payload.size()});
    REQUIRE(opened.ok());
    CHECK(opened.payload.size() == 1);
    CHECK(opened.payload.data()[0] == std::byte{0x42});
}

TEST_CASE("openssl_aead_packet_protector round trips through long packet pipeline with production policy") {
    auto material = make_test_material();
    REQUIRE(material.ok());
    flowq::quic::openssl_aead_packet_protector protector;
    REQUIRE(flowq::quic::openssl_aead_packet_protector::create(
        protector,
        flowq::quic::protection_level::handshake,
        std::move(material)).ok());

    flowq::quic::packet_build_request request{
        flowq::quic::long_packet_type::handshake,
        1,
        cid({0x83, 0x94, 0xc8, 0xf0, 0x3e, 0x51, 0x57, 0x08}),
        cid({0x11, 0x22, 0x33, 0x44}),
        {},
        flowq::quic::packet_number{flowq::quic::packet_number_space::handshake, 3},
        {
            flowq::quic::frame{flowq::quic::crypto_frame{0, flowq::buffer{bytes({0x01, 0x02, 0x03})}}},
            flowq::quic::frame{flowq::quic::ping_frame{}}
        },
        &protector,
        flowq::quic::packet_pipeline_config{1200},
        flowq::quic::packet_protection_policy::production_required
    };

    auto assembled = flowq::quic::assemble_long_packet(request);
    REQUIRE(assembled.ok());
    CHECK(assembled.protection == flowq::quic::protection_level::handshake);

    auto parsed = flowq::quic::parse_long_packet(
        assembled.datagram,
        &protector,
        flowq::quic::packet_protection_policy::production_required);
    REQUIRE(parsed.ok());
    CHECK(parsed.number.value == 3);
    CHECK(parsed.space == flowq::quic::packet_number_space::handshake);
    REQUIRE(parsed.frames.size() == 2);
    CHECK(std::holds_alternative<flowq::quic::crypto_frame>(parsed.frames[0]));
    CHECK(std::holds_alternative<flowq::quic::ping_frame>(parsed.frames[1]));
}

TEST_CASE("openssl_aead_packet_protector rejects tampered datagram through pipeline") {
    auto material = make_test_material();
    REQUIRE(material.ok());
    flowq::quic::openssl_aead_packet_protector protector;
    REQUIRE(flowq::quic::openssl_aead_packet_protector::create(
        protector,
        flowq::quic::protection_level::handshake,
        std::move(material)).ok());

    flowq::quic::packet_build_request request{
        flowq::quic::long_packet_type::handshake,
        1,
        cid({0x83, 0x94, 0xc8, 0xf0, 0x3e, 0x51, 0x57, 0x08}),
        cid({0x11, 0x22}),
        {},
        flowq::quic::packet_number{flowq::quic::packet_number_space::handshake, 7},
        {flowq::quic::frame{flowq::quic::ping_frame{}}},
        &protector,
        flowq::quic::packet_pipeline_config{1200},
        flowq::quic::packet_protection_policy::production_required
    };

    auto assembled = flowq::quic::assemble_long_packet(request);
    REQUIRE(assembled.ok());

    // Tamper with the last byte of the datagram
    auto tampered = assembled.datagram;
    tampered.data()[tampered.size() - 1U] ^= std::byte{0x01};

    auto parsed = flowq::quic::parse_long_packet(
        tampered,
        &protector,
        flowq::quic::packet_protection_policy::production_required);
    CHECK_FALSE(parsed.ok());
}

#else

TEST_CASE("openssl_aead_packet_protector fails-closed when crypto backend is disabled") {
    flowq::quic::openssl_aead_packet_protector protector;
    auto material = flowq::quic::traffic_key_material{};
    material.key = flowq::buffer{bytes({0x01, 0x02})};
    material.iv = flowq::buffer{bytes({0x03, 0x04})};
    material.header_protection_key = flowq::buffer{bytes({0x05, 0x06})};
    material.suite = flowq::quic::cipher_suite::aes_128_gcm_sha256;

    auto result = flowq::quic::openssl_aead_packet_protector::create(
        protector,
        flowq::quic::protection_level::handshake,
        std::move(material));
    // With crypto disabled, key lengths won't match (2 != 16), so creation fails
    CHECK_FALSE(result.ok());
}

#endif
