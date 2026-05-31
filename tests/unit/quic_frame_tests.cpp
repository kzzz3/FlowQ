#include <flowq/quic/frame.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
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

TEST_CASE("QUIC frame codec round trips application CONNECTION_CLOSE") {
    flowq::quic::application_close_frame frame{
        0x123,
        "app done"
    };

    auto encoded = flowq::quic::encode_frame(frame);
    REQUIRE(encoded.ok());
    REQUIRE(encoded.payload.size() >= 1);
    CHECK(encoded.payload.data()[0] == std::byte{0x1d});

    auto decoded = flowq::quic::decode_frames(encoded.payload);

    REQUIRE(decoded.ok());
    REQUIRE(decoded.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::application_close_frame>(decoded.frames[0]));

    const auto& close = std::get<flowq::quic::application_close_frame>(decoded.frames[0]);
    CHECK(close.error_code == 0x123);
    CHECK(close.reason == "app done");
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
    CHECK_FALSE(ack.ecn_counts.has_value());
    REQUIRE(ack.ranges.size() == 1);
    CHECK(ack.ranges[0].gap == 1);
    CHECK(ack.ranges[0].length == 2);
}

TEST_CASE("QUIC frame codec round trips ACK ECN counts") {
    flowq::quic::ack_frame frame{
        200,
        3,
        8,
        {flowq::quic::ack_range{0, 1}},
        flowq::quic::ack_ecn_counts{10, 11, 2}
    };

    auto encoded = flowq::quic::encode_frame(frame);
    REQUIRE(encoded.ok());
    REQUIRE(encoded.payload.size() >= 1);
    CHECK(encoded.payload.data()[0] == std::byte{0x03});

    auto decoded = flowq::quic::decode_frames(encoded.payload);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::ack_frame>(decoded.frames[0]));

    const auto& ack = std::get<flowq::quic::ack_frame>(decoded.frames[0]);
    CHECK(ack.largest_acknowledged == 200);
    CHECK(ack.ack_delay == 3);
    CHECK(ack.first_ack_range == 8);
    REQUIRE(ack.ranges.size() == 1);
    CHECK(ack.ranges[0].gap == 0);
    CHECK(ack.ranges[0].length == 1);
    REQUIRE(ack.ecn_counts.has_value());
    CHECK(ack.ecn_counts->ect0 == 10);
    CHECK(ack.ecn_counts->ect1 == 11);
    CHECK(ack.ecn_counts->ce == 2);
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

TEST_CASE("QUIC frame codec round trips flow-control frame values") {
    {
        flowq::quic::max_data_frame frame{1024};
        auto encoded = flowq::quic::encode_frame(frame);
        REQUIRE(encoded.ok());

        auto decoded = flowq::quic::decode_frames(encoded.payload);
        REQUIRE(decoded.ok());
        REQUIRE(decoded.frames.size() == 1);
        REQUIRE(std::holds_alternative<flowq::quic::max_data_frame>(decoded.frames[0]));
        CHECK(std::get<flowq::quic::max_data_frame>(decoded.frames[0]).maximum_data == 1024);
    }

    {
        flowq::quic::max_stream_data_frame frame{4, 2048};
        auto encoded = flowq::quic::encode_frame(frame);
        REQUIRE(encoded.ok());

        auto decoded = flowq::quic::decode_frames(encoded.payload);
        REQUIRE(decoded.ok());
        REQUIRE(decoded.frames.size() == 1);
        REQUIRE(std::holds_alternative<flowq::quic::max_stream_data_frame>(decoded.frames[0]));
        const auto& flow = std::get<flowq::quic::max_stream_data_frame>(decoded.frames[0]);
        CHECK(flow.stream_id == 4);
        CHECK(flow.maximum_stream_data == 2048);
    }

    {
        flowq::quic::data_blocked_frame frame{4096};
        auto encoded = flowq::quic::encode_frame(frame);
        REQUIRE(encoded.ok());

        auto decoded = flowq::quic::decode_frames(encoded.payload);
        REQUIRE(decoded.ok());
        REQUIRE(decoded.frames.size() == 1);
        REQUIRE(std::holds_alternative<flowq::quic::data_blocked_frame>(decoded.frames[0]));
        CHECK(std::get<flowq::quic::data_blocked_frame>(decoded.frames[0]).maximum_data == 4096);
    }

    {
        flowq::quic::stream_data_blocked_frame frame{8, 8192};
        auto encoded = flowq::quic::encode_frame(frame);
        REQUIRE(encoded.ok());

        auto decoded = flowq::quic::decode_frames(encoded.payload);
        REQUIRE(decoded.ok());
        REQUIRE(decoded.frames.size() == 1);
        REQUIRE(std::holds_alternative<flowq::quic::stream_data_blocked_frame>(decoded.frames[0]));
        const auto& blocked = std::get<flowq::quic::stream_data_blocked_frame>(decoded.frames[0]);
        CHECK(blocked.stream_id == 8);
        CHECK(blocked.maximum_stream_data == 8192);
    }

    {
        flowq::quic::max_streams_frame frame{flowq::quic::stream_direction::bidirectional, 16};
        auto encoded = flowq::quic::encode_frame(frame);
        REQUIRE(encoded.ok());

        auto decoded = flowq::quic::decode_frames(encoded.payload);
        REQUIRE(decoded.ok());
        REQUIRE(decoded.frames.size() == 1);
        REQUIRE(std::holds_alternative<flowq::quic::max_streams_frame>(decoded.frames[0]));
        const auto& streams = std::get<flowq::quic::max_streams_frame>(decoded.frames[0]);
        CHECK(streams.direction == flowq::quic::stream_direction::bidirectional);
        CHECK(streams.maximum_streams == 16);
    }

    {
        flowq::quic::streams_blocked_frame frame{flowq::quic::stream_direction::unidirectional, 8};
        auto encoded = flowq::quic::encode_frame(frame);
        REQUIRE(encoded.ok());

        auto decoded = flowq::quic::decode_frames(encoded.payload);
        REQUIRE(decoded.ok());
        REQUIRE(decoded.frames.size() == 1);
        REQUIRE(std::holds_alternative<flowq::quic::streams_blocked_frame>(decoded.frames[0]));
        const auto& blocked = std::get<flowq::quic::streams_blocked_frame>(decoded.frames[0]);
        CHECK(blocked.direction == flowq::quic::stream_direction::unidirectional);
        CHECK(blocked.maximum_streams == 8);
    }
}

TEST_CASE("QUIC frame codec round trips token connection-id and handshake-done frames") {
    {
        flowq::quic::new_token_frame frame{flowq::buffer{bytes({0xde, 0xad, 0xbe, 0xef})}};
        auto encoded = flowq::quic::encode_frame(frame);
        REQUIRE(encoded.ok());
        REQUIRE(encoded.payload.size() == 6);
        CHECK(encoded.payload.data()[0] == std::byte{0x07});

        auto decoded = flowq::quic::decode_frames(encoded.payload);
        REQUIRE(decoded.ok());
        REQUIRE(decoded.frames.size() == 1);
        REQUIRE(std::holds_alternative<flowq::quic::new_token_frame>(decoded.frames[0]));
        const auto& token = std::get<flowq::quic::new_token_frame>(decoded.frames[0]);
        REQUIRE(token.token.size() == 4);
        CHECK(token.token.data()[0] == std::byte{0xde});
        CHECK(token.token.data()[3] == std::byte{0xef});
    }

    {
        flowq::quic::new_connection_id_frame frame{
            3,
            1,
            flowq::quic::connection_id{flowq::buffer{bytes({0xaa, 0xbb, 0xcc, 0xdd})}},
            flowq::buffer{bytes({0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f})}
        };
        auto encoded = flowq::quic::encode_frame(frame);
        REQUIRE(encoded.ok());

        auto decoded = flowq::quic::decode_frames(encoded.payload);
        REQUIRE(decoded.ok());
        REQUIRE(decoded.frames.size() == 1);
        REQUIRE(std::holds_alternative<flowq::quic::new_connection_id_frame>(decoded.frames[0]));
        const auto& cid = std::get<flowq::quic::new_connection_id_frame>(decoded.frames[0]);
        CHECK(cid.sequence_number == 3);
        CHECK(cid.retire_prior_to == 1);
        CHECK(cid.connection_id.bytes.size() == 4);
        CHECK(cid.stateless_reset_token.size() == 16);
    }

    {
        flowq::quic::retire_connection_id_frame frame{2};
        auto encoded = flowq::quic::encode_frame(frame);
        REQUIRE(encoded.ok());

        auto decoded = flowq::quic::decode_frames(encoded.payload);
        REQUIRE(decoded.ok());
        REQUIRE(decoded.frames.size() == 1);
        REQUIRE(std::holds_alternative<flowq::quic::retire_connection_id_frame>(decoded.frames[0]));
        CHECK(std::get<flowq::quic::retire_connection_id_frame>(decoded.frames[0]).sequence_number == 2);
    }

    {
        auto encoded = flowq::quic::encode_frame(flowq::quic::handshake_done_frame{});
        REQUIRE(encoded.ok());

        auto decoded = flowq::quic::decode_frames(encoded.payload);
        REQUIRE(decoded.ok());
        REQUIRE(decoded.frames.size() == 1);
        REQUIRE(std::holds_alternative<flowq::quic::handshake_done_frame>(decoded.frames[0]));
    }
}

TEST_CASE("QUIC frame codec round trips PATH_CHALLENGE and PATH_RESPONSE frames") {
    const std::array<std::byte, 8> challenge_data{
        std::byte{0x10}, std::byte{0x11}, std::byte{0x12}, std::byte{0x13},
        std::byte{0x14}, std::byte{0x15}, std::byte{0x16}, std::byte{0x17}
    };
    const std::array<std::byte, 8> response_data{
        std::byte{0x20}, std::byte{0x21}, std::byte{0x22}, std::byte{0x23},
        std::byte{0x24}, std::byte{0x25}, std::byte{0x26}, std::byte{0x27}
    };

    {
        flowq::quic::path_challenge_frame frame{challenge_data};
        auto encoded = flowq::quic::encode_frame(frame);
        REQUIRE(encoded.ok());
        REQUIRE(encoded.payload.size() == 9);
        CHECK(encoded.payload.data()[0] == std::byte{0x1a});

        auto decoded = flowq::quic::decode_frames(encoded.payload);
        REQUIRE(decoded.ok());
        REQUIRE(decoded.frames.size() == 1);
        REQUIRE(std::holds_alternative<flowq::quic::path_challenge_frame>(decoded.frames[0]));
        CHECK(std::get<flowq::quic::path_challenge_frame>(decoded.frames[0]).data == challenge_data);
    }

    {
        flowq::quic::path_response_frame frame{response_data};
        auto encoded = flowq::quic::encode_frame(frame);
        REQUIRE(encoded.ok());
        REQUIRE(encoded.payload.size() == 9);
        CHECK(encoded.payload.data()[0] == std::byte{0x1b});

        auto decoded = flowq::quic::decode_frames(encoded.payload);
        REQUIRE(decoded.ok());
        REQUIRE(decoded.frames.size() == 1);
        REQUIRE(std::holds_alternative<flowq::quic::path_response_frame>(decoded.frames[0]));
        CHECK(std::get<flowq::quic::path_response_frame>(decoded.frames[0]).data == response_data);
    }
}

TEST_CASE("QUIC frame codec round trips RESET_STREAM and STOP_SENDING structurally") {
    {
        flowq::quic::reset_stream_frame frame{4, 0x42, 12};
        auto encoded = flowq::quic::encode_frame(frame);
        REQUIRE(encoded.ok());

        auto decoded = flowq::quic::decode_frames(encoded.payload);
        REQUIRE(decoded.ok());
        REQUIRE(decoded.frames.size() == 1);
        REQUIRE(std::holds_alternative<flowq::quic::reset_stream_frame>(decoded.frames[0]));
        const auto& reset = std::get<flowq::quic::reset_stream_frame>(decoded.frames[0]);
        CHECK(reset.stream_id == 4);
        CHECK(reset.application_error_code == 0x42);
        CHECK(reset.final_size == 12);
    }

    {
        flowq::quic::stop_sending_frame frame{4, 0x42};
        auto encoded = flowq::quic::encode_frame(frame);
        REQUIRE(encoded.ok());

        auto decoded = flowq::quic::decode_frames(encoded.payload);
        REQUIRE(decoded.ok());
        REQUIRE(decoded.frames.size() == 1);
        REQUIRE(std::holds_alternative<flowq::quic::stop_sending_frame>(decoded.frames[0]));
        const auto& stop = std::get<flowq::quic::stop_sending_frame>(decoded.frames[0]);
        CHECK(stop.stream_id == 4);
        CHECK(stop.application_error_code == 0x42);
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

TEST_CASE("QUIC frame codec decodes mixed flow-control frames") {
    auto decoded = flowq::quic::decode_frames(bytes({
        0x01,
        0x10, 0x40, 0x40,
        0x11, 0x04, 0x48, 0x00,
        0x14, 0x50, 0x00,
        0x15, 0x08, 0x60, 0x00,
        0x0a, 0x01, 0x01, 0xee
    }));

    REQUIRE(decoded.ok());
    REQUIRE(decoded.frames.size() == 6);
    CHECK(std::holds_alternative<flowq::quic::ping_frame>(decoded.frames[0]));
    REQUIRE(std::holds_alternative<flowq::quic::max_data_frame>(decoded.frames[1]));
    CHECK(std::get<flowq::quic::max_data_frame>(decoded.frames[1]).maximum_data == 64);
    REQUIRE(std::holds_alternative<flowq::quic::max_stream_data_frame>(decoded.frames[2]));
    CHECK(std::get<flowq::quic::max_stream_data_frame>(decoded.frames[2]).stream_id == 4);
    CHECK(std::get<flowq::quic::max_stream_data_frame>(decoded.frames[2]).maximum_stream_data == 2048);
    REQUIRE(std::holds_alternative<flowq::quic::data_blocked_frame>(decoded.frames[3]));
    CHECK(std::get<flowq::quic::data_blocked_frame>(decoded.frames[3]).maximum_data == 4096);
    REQUIRE(std::holds_alternative<flowq::quic::stream_data_blocked_frame>(decoded.frames[4]));
    CHECK(std::get<flowq::quic::stream_data_blocked_frame>(decoded.frames[4]).stream_id == 8);
    CHECK(std::get<flowq::quic::stream_data_blocked_frame>(decoded.frames[4]).maximum_stream_data == 8192);
    CHECK(std::holds_alternative<flowq::quic::stream_frame>(decoded.frames[5]));
}

TEST_CASE("QUIC frame codec decodes mixed close reset and stream frames") {
    auto decoded = flowq::quic::decode_frames(bytes({
        0x01,
        0x04, 0x04, 0x2a, 0x0c,
        0x05, 0x04, 0x2a,
        0x1c, 0x00, 0x00, 0x00,
        0x0a, 0x01, 0x01, 0xee
    }));

    REQUIRE(decoded.ok());
    REQUIRE(decoded.frames.size() == 5);
    CHECK(std::holds_alternative<flowq::quic::ping_frame>(decoded.frames[0]));
    REQUIRE(std::holds_alternative<flowq::quic::reset_stream_frame>(decoded.frames[1]));
    CHECK(std::get<flowq::quic::reset_stream_frame>(decoded.frames[1]).final_size == 12);
    REQUIRE(std::holds_alternative<flowq::quic::stop_sending_frame>(decoded.frames[2]));
    CHECK(std::get<flowq::quic::stop_sending_frame>(decoded.frames[2]).application_error_code == 0x2a);
    CHECK(std::holds_alternative<flowq::quic::connection_close_frame>(decoded.frames[3]));
    CHECK(std::holds_alternative<flowq::quic::stream_frame>(decoded.frames[4]));
}

TEST_CASE("QUIC frame codec reports empty unknown and truncated frames") {
    CHECK_FALSE(flowq::quic::decode_frames(std::span<const std::byte>{}).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x1f})).ok());

    // CONNECTION_CLOSE with frame type and missing reason phrase length.
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x1c, 0x0a, 0x01})).ok());

    // CONNECTION_CLOSE with reason length 4 but only one byte present.
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x1c, 0x0a, 0x01, 0x04, 0x78})).ok());

    // APPLICATION_CLOSE with error code and missing reason phrase length.
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x1d, 0x0a})).ok());

    // APPLICATION_CLOSE with reason length 4 but only one byte present.
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x1d, 0x0a, 0x04, 0x78})).ok());
}

TEST_CASE("QUIC frame codec rejects malformed ACK CRYPTO and STREAM frames") {
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x03, 0x00, 0x00, 0x00, 0x00})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x03, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02})).ok());

    // ACK range count is 1 but the Gap/Range pair is missing.
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x02, 0x64, 0x00, 0x01, 0x04})).ok());

    // ACK range count is the largest possible varint but no ranges follow.
    CHECK_NOTHROW([&] {
        auto decoded = flowq::quic::decode_frames(bytes({
            0x02,
            0x00,
            0x00,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0x00
        }));
        CHECK_FALSE(decoded.ok());
    }());

    // CRYPTO declares two bytes but only one byte is present.
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x06, 0x00, 0x02, 0xaa})).ok());

    // NEW_TOKEN has a length-prefixed, non-empty token.
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x07})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x07, 0x00})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x07, 0x02, 0xaa})).ok());

    // STREAM explicit length declares two bytes but only one byte is present.
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x0a, 0x01, 0x02, 0xaa})).ok());

    // Truncated nested varint.
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x06, 0x40})).ok());
}

TEST_CASE("QUIC frame codec rejects malformed flow-control frames") {
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x10})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x10, 0x40})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x11})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x11, 0x04})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x11, 0x04, 0x40})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x14})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x15})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x15, 0x08})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x15, 0x08, 0x40})).ok());
}

TEST_CASE("QUIC frame codec rejects malformed PATH validation frames") {
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x1a})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x1a, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x1b})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x1b, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06})).ok());
}

TEST_CASE("QUIC frame codec rejects malformed RESET_STREAM and STOP_SENDING frames") {
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x04})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x04, 0x04})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x04, 0x04, 0x42})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x04, 0x40})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x05})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x05, 0x04})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x05, 0x40})).ok());
}

TEST_CASE("QUIC frame codec rejects malformed stream-count and connection-id frames") {
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x12})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x13})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x16})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x17})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x18, 0x00, 0x00})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x18, 0x00, 0x00, 0x01, 0xaa})).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({
        0x18, 0x00, 0x00, 0x01, 0xaa,
        0x00, 0x01, 0x02, 0x03
    })).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({
        0x18, 0x01, 0x02, 0x01, 0xaa,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
    })).ok());
    CHECK_FALSE(flowq::quic::encode_frame(flowq::quic::new_connection_id_frame{
        1,
        2,
        flowq::quic::connection_id{flowq::buffer{bytes({0xaa})}},
        flowq::buffer{bytes({
            0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
            0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
        })}
    }).ok());
    CHECK_FALSE(flowq::quic::decode_frames(bytes({0x19})).ok());
}
