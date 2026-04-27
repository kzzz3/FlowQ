#include <flowq/quic/stream.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace {

flowq::buffer text(std::string value) {
    std::vector<std::byte> output;
    output.reserve(value.size());
    for (auto character : value) {
        output.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return flowq::buffer{output};
}

std::string as_string(const flowq::buffer& buffer) {
    std::string output;
    output.reserve(buffer.size());
    for (std::size_t index = 0; index < buffer.size(); ++index) {
        output.push_back(static_cast<char>(buffer.data()[index]));
    }
    return output;
}

flowq::quic::stream_frame stream(std::uint64_t id, std::uint64_t offset, std::string data, bool fin = false) {
    return flowq::quic::stream_frame{id, offset, true, true, fin, text(std::move(data))};
}

} // namespace

TEST_CASE("stream receive state delivers contiguous bytes in order") {
    flowq::quic::stream_receive_state state{};

    auto result = state.receive(stream(0, 0, "he"));

    REQUIRE(result.ok());
    CHECK(as_string(result.data) == "he");
    CHECK(state.next_offset() == 2);
    CHECK_FALSE(result.closed);
}

TEST_CASE("stream receive state buffers gaps until the prefix arrives") {
    flowq::quic::stream_receive_state state{};

    auto gap = state.receive(stream(0, 2, "llo"));
    REQUIRE(gap.ok());
    CHECK(gap.data.empty());
    CHECK(state.next_offset() == 0);

    auto prefix = state.receive(stream(0, 0, "he"));
    REQUIRE(prefix.ok());
    CHECK(as_string(prefix.data) == "hello");
    CHECK(state.next_offset() == 5);
}

TEST_CASE("stream receive state keeps overlapping buffered gaps from redelivering") {
    flowq::quic::stream_receive_state state{};

    REQUIRE(state.receive(stream(0, 2, "llo")).ok());
    REQUIRE(state.receive(stream(0, 1, "ello")).ok());
    auto prefix = state.receive(stream(0, 0, "h"));
    REQUIRE(prefix.ok());
    CHECK(as_string(prefix.data) == "hello");

    auto duplicate = state.receive(stream(0, 2, "llo"));
    REQUIRE(duplicate.ok());
    CHECK(duplicate.data.empty());
    CHECK(state.next_offset() == 5);
}

TEST_CASE("stream receive state merges buffered overlap behind a later prefix") {
    flowq::quic::stream_receive_state state{};

    REQUIRE(state.receive(stream(0, 5, "fghij")).ok());
    REQUIRE(state.receive(stream(0, 3, "defgh")).ok());
    auto prefix = state.receive(stream(0, 0, "abc"));

    REQUIRE(prefix.ok());
    CHECK(as_string(prefix.data) == "abcdefghij");
    CHECK(state.next_offset() == 10);
}

TEST_CASE("stream receive state merges a bridge between delivered and pending data") {
    flowq::quic::stream_receive_state state{};

    REQUIRE(state.receive(stream(0, 0, "abcde")).ok());
    REQUIRE(state.receive(stream(0, 7, "hij")).ok());
    auto bridge = state.receive(stream(0, 5, "fgh"));

    REQUIRE(bridge.ok());
    CHECK(as_string(bridge.data) == "fghij");
    CHECK(state.next_offset() == 10);
}

TEST_CASE("stream receive state rejects conflicting overlap against pending data") {
    flowq::quic::stream_receive_state state{};

    REQUIRE(state.receive(stream(0, 5, "fghij")).ok());
    auto conflict = state.receive(stream(0, 3, "deX"));

    CHECK_FALSE(conflict.ok());
    CHECK(conflict.error.code() == flowq::error_code::protocol_error);
    CHECK(state.next_offset() == 0);
}

TEST_CASE("stream receive state treats absent STREAM offset as zero") {
    flowq::quic::stream_receive_state state{};
    flowq::quic::stream_frame frame{0, 99, false, true, false, text("zero")};

    auto result = state.receive(frame);

    REQUIRE(result.ok());
    CHECK(as_string(result.data) == "zero");
    CHECK(state.next_offset() == 4);
}

TEST_CASE("stream receive state ignores exact duplicate data") {
    flowq::quic::stream_receive_state state{};

    REQUIRE(state.receive(stream(0, 0, "hello")).ok());
    auto duplicate = state.receive(stream(0, 0, "hello"));

    REQUIRE(duplicate.ok());
    CHECK(duplicate.data.empty());
    CHECK(state.next_offset() == 5);
}

TEST_CASE("stream receive state accepts identical overlap and delivers new suffix") {
    flowq::quic::stream_receive_state state{};

    REQUIRE(state.receive(stream(0, 0, "hello")).ok());
    auto overlap = state.receive(stream(0, 3, "lo!"));

    REQUIRE(overlap.ok());
    CHECK(as_string(overlap.data) == "!");
    CHECK(state.next_offset() == 6);
}

TEST_CASE("stream receive state rejects conflicting overlap") {
    flowq::quic::stream_receive_state state{};

    REQUIRE(state.receive(stream(0, 0, "hello")).ok());
    auto conflict = state.receive(stream(0, 3, "xy"));

    CHECK_FALSE(conflict.ok());
    CHECK(conflict.error.code() == flowq::error_code::protocol_error);
}

TEST_CASE("stream receive state tracks FIN final size and closed state") {
    flowq::quic::stream_receive_state state{};

    auto result = state.receive(stream(0, 0, "done", true));

    REQUIRE(result.ok());
    CHECK(as_string(result.data) == "done");
    CHECK(result.final_size_known);
    CHECK(result.final_size == 4);
    CHECK(result.closed);
    CHECK(state.closed());
}

TEST_CASE("stream receive state rejects inconsistent FIN final size") {
    flowq::quic::stream_receive_state state{};

    REQUIRE(state.receive(stream(0, 0, "done", true)).ok());
    auto result = state.receive(stream(0, 0, "done!", true));

    CHECK_FALSE(result.ok());
    CHECK(result.error.code() == flowq::error_code::protocol_error);
}

TEST_CASE("stream receive state rejects data beyond known final size") {
    flowq::quic::stream_receive_state state{};

    REQUIRE(state.receive(stream(0, 0, "done", true)).ok());
    auto result = state.receive(stream(0, 4, "!"));

    CHECK_FALSE(result.ok());
    CHECK(result.error.code() == flowq::error_code::protocol_error);
}

TEST_CASE("stream ID classification follows QUIC initiator and direction bits") {
    auto zero = flowq::quic::classify_stream_id(0);
    CHECK(zero.initiator == flowq::quic::stream_initiator::client);
    CHECK(zero.direction == flowq::quic::stream_direction::bidirectional);

    auto one = flowq::quic::classify_stream_id(1);
    CHECK(one.initiator == flowq::quic::stream_initiator::server);
    CHECK(one.direction == flowq::quic::stream_direction::bidirectional);

    auto two = flowq::quic::classify_stream_id(2);
    CHECK(two.initiator == flowq::quic::stream_initiator::client);
    CHECK(two.direction == flowq::quic::stream_direction::unidirectional);

    auto three = flowq::quic::classify_stream_id(3);
    CHECK(three.initiator == flowq::quic::stream_initiator::server);
    CHECK(three.direction == flowq::quic::stream_direction::unidirectional);
}

TEST_CASE("stream receive set routes stream frames independently") {
    flowq::quic::stream_receive_set streams{};

    auto first = streams.receive(stream(0, 0, "a"));
    auto second = streams.receive(stream(4, 0, "b"));

    REQUIRE(first.result.ok());
    REQUIRE(second.result.ok());
    CHECK(first.stream_id == 0);
    CHECK(second.stream_id == 4);
    CHECK(as_string(first.result.data) == "a");
    CHECK(as_string(second.result.data) == "b");
    REQUIRE(streams.find(0) != nullptr);
    REQUIRE(streams.find(4) != nullptr);
    CHECK(streams.find(0)->next_offset() == 1);
    CHECK(streams.find(4)->next_offset() == 1);
}
