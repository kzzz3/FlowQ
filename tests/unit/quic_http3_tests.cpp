#include <flowq/quic/http3.hpp>
#include <flowq/quic/http3_request.hpp>

#include <catch2/catch_test_macros.hpp>

#include <vector>

TEST_CASE("http3 DATA frame encodes correctly") {
    flowq::buffer payload{std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}}};
    auto encoded = flowq::quic::http3::encode_data_frame(payload);

    // Type (0x00) + Length (3) + Payload (3 bytes)
    REQUIRE(encoded.size() == 5);
    CHECK(encoded.data()[0] == std::byte{0x00});  // DATA type
    CHECK(encoded.data()[1] == std::byte{0x03});  // Length = 3
    CHECK(encoded.data()[2] == std::byte{0x01});  // Payload byte 0
    CHECK(encoded.data()[3] == std::byte{0x02});  // Payload byte 1
    CHECK(encoded.data()[4] == std::byte{0x03});  // Payload byte 2
}

TEST_CASE("http3 GOAWAY frame encodes correctly") {
    auto encoded = flowq::quic::http3::encode_goaway_frame(42);

    // Type (0x07) + Length (1 byte varint) + Stream ID (1 byte varint)
    REQUIRE(encoded.size() == 3);
    CHECK(encoded.data()[0] == std::byte{0x07});  // GOAWAY type
    CHECK(encoded.data()[1] == std::byte{0x01});  // Length = 1
    CHECK(encoded.data()[2] == std::byte{0x2a});  // Stream ID = 42
}

TEST_CASE("http3 GOAWAY frame length matches multi-byte stream ID varint") {
    auto encoded = flowq::quic::http3::encode_goaway_frame(64);

    // Stream ID 64 is a two-byte QUIC varint, so GOAWAY payload length is 2.
    REQUIRE(encoded.size() == 4);
    CHECK(encoded.data()[0] == std::byte{0x07});
    CHECK(encoded.data()[1] == std::byte{0x02});
    CHECK(encoded.data()[2] == std::byte{0x40});
    CHECK(encoded.data()[3] == std::byte{0x40});
}

TEST_CASE("http3 SETTINGS frame encodes correctly") {
    flowq::quic::http3::settings s{};
    s.max_field_section_size = 16384;
    s.qpack_max_table_capacity = 0;
    s.qpack_blocked_streams = 0;

    auto encoded = flowq::quic::http3::encode_settings_frame(s);

    // Type (0x04) + Length + Settings entries
    REQUIRE(encoded.size() > 1);
    CHECK(encoded.data()[0] == std::byte{0x04});  // SETTINGS type
}

TEST_CASE("http3 frame_type enum values match RFC") {
    CHECK(static_cast<std::uint64_t>(flowq::quic::http3::frame_type::data) == 0x00);
    CHECK(static_cast<std::uint64_t>(flowq::quic::http3::frame_type::headers) == 0x01);
    CHECK(static_cast<std::uint64_t>(flowq::quic::http3::frame_type::settings) == 0x04);
    CHECK(static_cast<std::uint64_t>(flowq::quic::http3::frame_type::goaway) == 0x07);
}

TEST_CASE("http3 error_code enum values match RFC") {
    CHECK(static_cast<std::uint64_t>(flowq::quic::http3::error_code::no_error) == 0x0100);
    CHECK(static_cast<std::uint64_t>(flowq::quic::http3::error_code::general_protocol_error) == 0x0101);
    CHECK(static_cast<std::uint64_t>(flowq::quic::http3::error_code::internal_error) == 0x0102);
}

TEST_CASE("http3 settings has correct defaults") {
    flowq::quic::http3::settings s{};
    CHECK(s.max_field_section_size == 16384);
    CHECK(s.qpack_max_table_capacity == 0);
    CHECK(s.qpack_blocked_streams == 0);
}

TEST_CASE("http3 request decoder rejects empty HEADERS frame") {
    flowq::quic::http3::request_decoder decoder{};
    std::vector<flowq::quic::http3::http3_frame_variant> frames;
    frames.emplace_back(flowq::quic::http3::headers_frame{flowq::quic::http3::header_block{}});

    CHECK_FALSE(decoder.decode(frames).has_value());
}

TEST_CASE("http3 response decoder rejects empty HEADERS frame") {
    flowq::quic::http3::response_decoder decoder{};
    std::vector<flowq::quic::http3::http3_frame_variant> frames;
    frames.emplace_back(flowq::quic::http3::headers_frame{flowq::quic::http3::header_block{}});

    CHECK_FALSE(decoder.decode(frames).has_value());
}
