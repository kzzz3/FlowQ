#include <flowq/quic/packet_header.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <array>
#include <initializer_list>
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

void check_buffer(const flowq::buffer& buffer, std::initializer_list<unsigned int> expected) {
    REQUIRE(buffer.size() == expected.size());
    auto index = std::size_t{0};
    for (auto value : expected) {
        CHECK(buffer.data()[index] == static_cast<std::byte>(value & 0xffU));
        ++index;
    }
}

} // namespace

TEST_CASE("packet header decodes Version Negotiation versions and CIDs") {
    auto decoded = flowq::quic::decode_packet_header(bytes({
        0x80,
        0x00, 0x00, 0x00, 0x00,
        0x02, 0xaa, 0xbb,
        0x01, 0xcc,
        0x00, 0x00, 0x00, 0x01,
        0x70, 0x9a, 0x50, 0xc4
    }));

    REQUIRE(decoded.ok());
    REQUIRE(std::holds_alternative<flowq::quic::version_negotiation_header>(decoded.header));

    const auto& header = std::get<flowq::quic::version_negotiation_header>(decoded.header);
    CHECK(header.first_byte == std::byte{0x80});
    check_buffer(header.destination_connection_id.bytes, {0xaa, 0xbb});
    check_buffer(header.source_connection_id.bytes, {0xcc});
    REQUIRE(header.supported_versions.size() == 2);
    CHECK(header.supported_versions[0] == 0x00000001U);
    CHECK(header.supported_versions[1] == 0x709a50c4U);
}

TEST_CASE("packet header encodes Version Negotiation and decodes it back") {
    flowq::quic::version_negotiation_header header{
        std::byte{0x80},
        flowq::quic::connection_id{flowq::buffer{bytes({0xaa})}},
        flowq::quic::connection_id{flowq::buffer{bytes({0xbb, 0xcc})}},
        {0x00000001U, 0x709a50c4U}
    };

    auto encoded = flowq::quic::encode_packet_header(flowq::quic::packet_header{header});
    REQUIRE(encoded.ok());

    auto decoded = flowq::quic::decode_packet_header(encoded.payload);
    REQUIRE(decoded.ok());
    REQUIRE(std::holds_alternative<flowq::quic::version_negotiation_header>(decoded.header));
    const auto& roundtrip = std::get<flowq::quic::version_negotiation_header>(decoded.header);
    check_buffer(roundtrip.destination_connection_id.bytes, {0xaa});
    check_buffer(roundtrip.source_connection_id.bytes, {0xbb, 0xcc});
    CHECK(roundtrip.supported_versions == header.supported_versions);
}

TEST_CASE("packet header decodes Initial token and opaque protected payload") {
    auto decoded = flowq::quic::decode_packet_header(bytes({
        0xc0,
        0x00, 0x00, 0x00, 0x01,
        0x01, 0xaa,
        0x01, 0xbb,
        0x02, 0x11, 0x22,
        0x03, 0x01, 0x02, 0x03
    }));

    REQUIRE(decoded.ok());
    REQUIRE(std::holds_alternative<flowq::quic::initial_header>(decoded.header));

    const auto& header = std::get<flowq::quic::initial_header>(decoded.header);
    CHECK(header.first_byte == std::byte{0xc0});
    CHECK(header.version == 1U);
    check_buffer(header.destination_connection_id.bytes, {0xaa});
    check_buffer(header.source_connection_id.bytes, {0xbb});
    check_buffer(header.token, {0x11, 0x22});
    CHECK(header.length == 3);
    check_buffer(header.protected_payload, {0x01, 0x02, 0x03});
}

TEST_CASE("packet header encodes Initial and decodes it back") {
    flowq::quic::initial_header header{
        std::byte{0xc0},
        1,
        flowq::quic::connection_id{flowq::buffer{bytes({0xaa})}},
        flowq::quic::connection_id{flowq::buffer{bytes({0xbb})}},
        flowq::buffer{bytes({0x11, 0x22})},
        3,
        flowq::buffer{bytes({0x01, 0x02, 0x03})}
    };

    auto encoded = flowq::quic::encode_packet_header(flowq::quic::packet_header{header});
    REQUIRE(encoded.ok());
    auto decoded = flowq::quic::decode_packet_header(encoded.payload);

    REQUIRE(decoded.ok());
    REQUIRE(std::holds_alternative<flowq::quic::initial_header>(decoded.header));
    const auto& roundtrip = std::get<flowq::quic::initial_header>(decoded.header);
    check_buffer(roundtrip.token, {0x11, 0x22});
    check_buffer(roundtrip.protected_payload, {0x01, 0x02, 0x03});
}

TEST_CASE("packet header decodes Handshake protected payload") {
    auto decoded = flowq::quic::decode_packet_header(bytes({
        0xe0,
        0x00, 0x00, 0x00, 0x01,
        0x00,
        0x00,
        0x02, 0xab, 0xcd
    }));

    REQUIRE(decoded.ok());
    REQUIRE(std::holds_alternative<flowq::quic::handshake_header>(decoded.header));

    const auto& header = std::get<flowq::quic::handshake_header>(decoded.header);
    CHECK(header.first_byte == std::byte{0xe0});
    CHECK(header.version == 1U);
    CHECK(header.length == 2);
    check_buffer(header.protected_payload, {0xab, 0xcd});
}

TEST_CASE("packet header encodes Handshake and decodes it back") {
    flowq::quic::handshake_header header{
        std::byte{0xe0},
        1,
        flowq::quic::connection_id{flowq::buffer{bytes({0xaa})}},
        flowq::quic::connection_id{flowq::buffer{bytes({0xbb})}},
        2,
        flowq::buffer{bytes({0xab, 0xcd})}
    };

    auto encoded = flowq::quic::encode_packet_header(flowq::quic::packet_header{header});
    REQUIRE(encoded.ok());

    auto decoded = flowq::quic::decode_packet_header(encoded.payload);
    REQUIRE(decoded.ok());
    REQUIRE(std::holds_alternative<flowq::quic::handshake_header>(decoded.header));

    const auto& roundtrip = std::get<flowq::quic::handshake_header>(decoded.header);
    CHECK(roundtrip.version == 1U);
    CHECK(roundtrip.length == 2);
    check_buffer(roundtrip.destination_connection_id.bytes, {0xaa});
    check_buffer(roundtrip.source_connection_id.bytes, {0xbb});
    check_buffer(roundtrip.protected_payload, {0xab, 0xcd});
}

TEST_CASE("packet header decodes Retry opaque tail") {
    auto decoded = flowq::quic::decode_packet_header(bytes({
        0xf0,
        0x00, 0x00, 0x00, 0x01,
        0x01, 0xaa,
        0x01, 0xbb,
        0xde, 0xad, 0xbe, 0xef
    }));

    REQUIRE(decoded.ok());
    REQUIRE(std::holds_alternative<flowq::quic::retry_header>(decoded.header));

    const auto& header = std::get<flowq::quic::retry_header>(decoded.header);
    CHECK(header.version == 1U);
    check_buffer(header.opaque_retry_tail, {0xde, 0xad, 0xbe, 0xef});
}

TEST_CASE("packet header reports malformed and unsupported inputs") {
    CHECK_FALSE(flowq::quic::decode_packet_header(std::span<const std::byte>{}).ok());
    CHECK_FALSE(flowq::quic::decode_packet_header(bytes({0x40})).ok());
    CHECK_FALSE(flowq::quic::decode_packet_header(bytes({0x80, 0x00})).ok());
    CHECK_FALSE(flowq::quic::decode_packet_header(bytes({0x80, 0x00, 0x00, 0x00, 0x00, 0x02, 0xaa})).ok());
    CHECK_FALSE(flowq::quic::decode_packet_header(bytes({0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})).ok());
    CHECK_FALSE(flowq::quic::decode_packet_header(bytes({0xc0, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x40})).ok());
    CHECK_FALSE(flowq::quic::decode_packet_header(bytes({0xc0, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0xab})).ok());
    CHECK_FALSE(flowq::quic::decode_packet_header(bytes({0xf0, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00})).ok());
    CHECK_FALSE(flowq::quic::decode_packet_header(bytes({0xd0, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00})).ok());
}

TEST_CASE("packet header keeps short and structural Application packets out of long-header decoding") {
    CHECK_FALSE(flowq::quic::decode_packet_header(bytes({0x40, 0x00, 0x00, 0x00, 0x00})).ok());
    CHECK_FALSE(flowq::quic::decode_packet_header(bytes({0x50, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00})).ok());
}

TEST_CASE("packet header encode rejects invalid structural values") {
    flowq::quic::version_negotiation_header no_versions{
        std::byte{0x80},
        flowq::quic::connection_id{flowq::buffer{bytes({0xaa})}},
        flowq::quic::connection_id{flowq::buffer{bytes({0xbb})}},
        {}
    };
    CHECK_FALSE(flowq::quic::encode_packet_header(flowq::quic::packet_header{no_versions}).ok());

    flowq::quic::handshake_header bad_length{
        std::byte{0xe0},
        1,
        flowq::quic::connection_id{},
        flowq::quic::connection_id{},
        3,
        flowq::buffer{bytes({0xab, 0xcd})}
    };
    CHECK_FALSE(flowq::quic::encode_packet_header(flowq::quic::packet_header{bad_length}).ok());
}

TEST_CASE("packet number length selection follows QUIC reconstruction window needs") {
    auto one_byte = flowq::quic::select_packet_number_length(0xabe8, 0xabd0);
    REQUIRE(one_byte.ok());
    CHECK(one_byte.bytes == 1);

    auto two_bytes = flowq::quic::select_packet_number_length(0x100c8, 0x10000);
    REQUIRE(two_bytes.ok());
    CHECK(two_bytes.bytes == 2);

    auto four_bytes = flowq::quic::select_packet_number_length(0x0100'0000ULL, 0);
    REQUIRE(four_bytes.ok());
    CHECK(four_bytes.bytes == 4);

    CHECK_FALSE(flowq::quic::select_packet_number_length(7, 7).ok());
}

TEST_CASE("packet number truncation writes selected least significant bytes") {
    std::array<std::byte, 4> output{};

    auto encoded = flowq::quic::encode_packet_number(0x12345678, 3, output);
    REQUIRE(encoded.ok());
    CHECK(encoded.bytes_written == 3);
    CHECK(output[0] == std::byte{0x34});
    CHECK(output[1] == std::byte{0x56});
    CHECK(output[2] == std::byte{0x78});

    CHECK_FALSE(flowq::quic::encode_packet_number(1, 0, output).ok());
    CHECK_FALSE(flowq::quic::encode_packet_number(1, 5, output).ok());
}

TEST_CASE("packet number reconstruction follows RFC 9000 sample arithmetic") {
    auto decoded = flowq::quic::decode_packet_number(0x9b32, 2, 0xa82f30ea);
    REQUIRE(decoded.ok());
    CHECK(decoded.packet_number == 0xa82f9b32ULL);

    auto low_candidate = flowq::quic::decode_packet_number(0x01, 1, 0xff);
    REQUIRE(low_candidate.ok());
    CHECK(low_candidate.packet_number == 0x101ULL);

    auto high_candidate = flowq::quic::decode_packet_number(0xf0, 1, 0x101);
    REQUIRE(high_candidate.ok());
    CHECK(high_candidate.packet_number == 0xf0ULL);

    CHECK_FALSE(flowq::quic::decode_packet_number(0, 0, 0).ok());
    CHECK_FALSE(flowq::quic::decode_packet_number(0, 5, 0).ok());
}
