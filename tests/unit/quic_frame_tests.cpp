#include <flowq/quic/frame.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <initializer_list>
#include <string>
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

} // namespace

TEST_CASE("QUIC frame codec decodes PADDING run and PING") {
    auto decoded = flowq::quic::decode_frames(bytes({0x00, 0x00, 0x01}));

    REQUIRE(decoded.ok());
    REQUIRE(decoded.frames.size() == 2);
    REQUIRE(std::holds_alternative<flowq::quic::padding_frame>(decoded.frames[0]));
    CHECK(std::get<flowq::quic::padding_frame>(decoded.frames[0]).count == 2);
    CHECK(std::holds_alternative<flowq::quic::ping_frame>(decoded.frames[1]));
}

TEST_CASE("QUIC frame codec round trips transport CONNECTION_CLOSE") {
    flowq::quic::connection_close_frame frame{
        0x0a,
        0x01,
        "closing"
    };

    auto encoded = flowq::quic::encode_frame(frame);
    REQUIRE(encoded.ok());

    auto decoded = flowq::quic::decode_frames(encoded.payload);

    REQUIRE(decoded.ok());
    REQUIRE(decoded.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::connection_close_frame>(decoded.frames[0]));

    const auto& close = std::get<flowq::quic::connection_close_frame>(decoded.frames[0]);
    CHECK(close.error_code == 0x0a);
    CHECK(close.frame_type == 0x01);
    CHECK(close.reason == "closing");
}

TEST_CASE("QUIC frame codec round trips ACK frames structurally") {
    flowq::quic::ack_frame frame{
        100,
        25,
        4,
        {flowq::quic::ack_range{1, 2}}
    };

    auto encoded = flowq::quic::encode_frame(frame);
    REQUIRE(encoded.ok());

    auto decoded = flowq::quic::decode_frames(encoded.payload);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::ack_frame>(decoded.frames[0]));

    const auto& ack = std::get<flowq::quic::ack_frame>(decoded.frames[0]);
    CHECK(ack.largest_acknowledged == 100);
    CHECK(ack.ack_delay == 25);
    CHECK(ack.first_ack_range == 4);
    REQUIRE(ack.ranges.size() == 1);
    CHECK(ack.ranges[0].gap == 1);
    CHECK(ack.ranges[0].length == 2);
}

TEST_CASE("QUIC frame codec round trips CRYPTO frames as opaque data") {
    flowq::quic::crypto_frame frame{7, flowq::buffer{bytes({0xde, 0xad, 0xbe, 0xef})}};

    auto encoded = flowq::quic::encode_frame(frame);
    REQUIRE(encoded.ok());

    auto decoded = flowq::quic::decode_frames(encoded.payload);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::crypto_frame>(decoded.frames[0]));

    const auto& crypto = std::get<flowq::quic::crypto_frame>(decoded.frames[0]);
    CHECK(crypto.offset == 7);
    REQUIRE(crypto.data.size() == 4);
    CHECK(crypto.data.data()[0] == std::byte{0xde});
    CHECK(crypto.data.data()[3] == std::byte{0xef});
}

TEST_CASE("QUIC frame codec round trips all STREAM type variants") {
    for (auto type = 0x08U; type <= 0x0fU; ++type) {
        const bool offset_present = (type & 0x04U) != 0;
        const bool length_present = (type & 0x02U) != 0;
        const bool fin = (type & 0x01U) != 0;
        flowq::quic::stream_frame frame{
            5,
            offset_present ? 9U : 0U,
            offset_present,
            length_present,
            fin,
            flowq::buffer{bytes({0xca, 0xfe})}
        };

        auto encoded = flowq::quic::encode_frame(frame);
        REQUIRE(encoded.ok());
        CHECK(encoded.payload.data()[0] == static_cast<std::byte>(type));

        auto decoded = flowq::quic::decode_frames(encoded.payload);
        REQUIRE(decoded.ok());
        REQUIRE(decoded.frames.size() == 1);
        REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(decoded.frames[0]));
        const auto& stream = std::get<flowq::quic::stream_frame>(decoded.frames[0]);
        CHECK(stream.stream_id == 5);
        CHECK(stream.offset == (offset_present ? 9U : 0U));
        CHECK(stream.offset_present == offset_present);
        CHECK(stream.length_present == length_present);
        CHECK(stream.fin == fin);
        REQUIRE(stream.data.size() == 2);
        CHECK(stream.data.data()[0] == std::byte{0xca});
        CHECK(stream.data.data()[1] == std::byte{0xfe});
    }
}

TEST_CASE("QUIC frame codec decodes mixed old and new frames") {
    auto decoded = flowq::quic::decode_frames(bytes({
        0x00,
        0x01,
        0x06, 0x00, 0x02, 0xaa, 0xbb,
        0x0a, 0x01, 0x02, 0xcc, 0xdd
    }));

    REQUIRE(decoded.ok());
    REQUIRE(decoded.frames.size() == 4);
    CHECK(std::holds_alternative<flowq::quic::padding_frame>(decoded.frames[0]));
    CHECK(std::holds_alternative<flowq::quic::ping_frame>(decoded.frames[1]));
    CHECK(std::holds_alternative<flowq::quic::crypto_frame>(decoded.frames[2]));
    CHECK(std::holds_alternative<flowq::quic::stream_frame>(decoded.frames[3]));
}

TEST_CASE("QUIC frame codec reports empty unknown and truncated frames") {
    CHECK_FALSE(flowq::quic::decode_frames(std::span<const std::byte>{}).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x1f})).ok());

    // CONNECTION_CLOSE with frame type and missing reason phrase length.
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x1c, 0x0a, 0x01})).ok());

    // CONNECTION_CLOSE with reason length 4 but only one byte present.
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x1c, 0x0a, 0x01, 0x04, 0x78})).ok());
}

TEST_CASE("QUIC frame codec rejects malformed ACK CRYPTO and STREAM frames") {
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x03, 0x00, 0x00, 0x00, 0x00})).ok());

    // ACK range count is 1 but the Gap/Range pair is missing.
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x02, 0x64, 0x00, 0x01, 0x04})).ok());

    // CRYPTO declares two bytes but only one byte is present.
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x06, 0x00, 0x02, 0xaa})).ok());

    // STREAM explicit length declares two bytes but only one byte is present.
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x0a, 0x01, 0x02, 0xaa})).ok());

    // Truncated nested varint.
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x06, 0x40})).ok());
}
