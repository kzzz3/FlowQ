#include <flowq/quic/initial_packet_protector.hpp>
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

} // namespace

TEST_CASE("Initial packet protector exposes packet-protection provider evidence only when backend is available") {
    auto protector = flowq::quic::initial_packet_protector::client(cid({0x83, 0x94, 0xc8, 0xf0, 0x3e, 0x51, 0x57, 0x08}));

    CHECK(protector.level() == flowq::quic::protection_level::initial);
    CHECK(protector.security_level() == flowq::quic::packet_security_level::authenticated_encrypted);

#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)
    CHECK(protector.provider_status().packet_protection_ready());
    CHECK_FALSE(protector.provider_status().production_ready());
#else
    CHECK_FALSE(protector.provider_status().packet_protection_ready());
    CHECK_FALSE(protector.provider_status().production_ready());
    CHECK_FALSE(protector.protect(bytes({0x01})).ok());
    CHECK_FALSE(protector.unprotect(bytes({0x00, 0x01})).ok());
#endif
}

#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)

TEST_CASE("Initial packet protector round trips Initial packets through pipeline under production policy") {
    auto protector = flowq::quic::initial_packet_protector::client(cid({0x83, 0x94, 0xc8, 0xf0, 0x3e, 0x51, 0x57, 0x08}));

    flowq::quic::packet_build_request request{
        flowq::quic::long_packet_type::initial,
        1,
        cid({0x83, 0x94, 0xc8, 0xf0, 0x3e, 0x51, 0x57, 0x08}),
        cid({0x11, 0x22, 0x33, 0x44}),
        {},
        flowq::quic::packet_number{flowq::quic::packet_number_space::initial, 2},
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
    CHECK(assembled.protection == flowq::quic::protection_level::initial);

    auto parsed = flowq::quic::parse_long_packet(
        assembled.datagram,
        &protector,
        flowq::quic::packet_protection_policy::production_required);
    REQUIRE(parsed.ok());
    CHECK(parsed.number.value == 2);
    CHECK(parsed.space == flowq::quic::packet_number_space::initial);
    REQUIRE(parsed.frames.size() == 2);
    CHECK(std::holds_alternative<flowq::quic::crypto_frame>(parsed.frames[0]));
    CHECK(std::holds_alternative<flowq::quic::ping_frame>(parsed.frames[1]));
}

TEST_CASE("Initial packet protector rejects altered Initial packet ciphertext") {
    auto protector = flowq::quic::initial_packet_protector::client(cid({0x83, 0x94, 0xc8, 0xf0, 0x3e, 0x51, 0x57, 0x08}));

    flowq::quic::packet_build_request request{
        flowq::quic::long_packet_type::initial,
        1,
        cid({0x83, 0x94, 0xc8, 0xf0, 0x3e, 0x51, 0x57, 0x08}),
        cid({0x11}),
        {},
        flowq::quic::packet_number{flowq::quic::packet_number_space::initial, 7},
        {flowq::quic::frame{flowq::quic::ping_frame{}}},
        &protector,
        flowq::quic::packet_pipeline_config{1200},
        flowq::quic::packet_protection_policy::production_required
    };

    auto assembled = flowq::quic::assemble_long_packet(request);
    REQUIRE(assembled.ok());

    auto tampered = assembled.datagram;
    tampered.data()[tampered.size() - 1U] ^= std::byte{0x01};

    auto parsed = flowq::quic::parse_long_packet(
        tampered,
        &protector,
        flowq::quic::packet_protection_policy::production_required);
    CHECK_FALSE(parsed.ok());
    CHECK(parsed.error.code() == flowq::error_code::tls_error);
}

#endif
