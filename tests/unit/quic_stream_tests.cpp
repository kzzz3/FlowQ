#include <flowq/quic/stream.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <variant>
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

TEST_CASE("stream receive state enforces stream data credit") {
    flowq::quic::stream_receive_state state{5};

    auto accepted = state.receive(stream(0, 0, "hello"));
    auto blocked = state.receive(stream(0, 5, "!"));

    REQUIRE(accepted.ok());
    CHECK(as_string(accepted.data) == "hello");
    CHECK_FALSE(blocked.ok());
    CHECK(blocked.error.code() == flowq::error_code::flow_control_error);
}

TEST_CASE("stream receive state applies RESET_STREAM final size") {
    flowq::quic::stream_receive_state state{};
    REQUIRE(state.receive(stream(0, 0, "he")).ok());

    auto reset = state.reset(flowq::quic::reset_stream_frame{0, 0x2a, 5});

    REQUIRE(reset.ok());
    CHECK(reset.final_size_known);
    CHECK(reset.final_size == 5);
    CHECK(reset.closed);
    CHECK(state.reset_received());
    CHECK(state.reset_error_code() == 0x2a);
    CHECK(state.reset_final_size() == 5);

    auto beyond = state.receive(stream(0, 5, "!"));
    CHECK_FALSE(beyond.ok());
    CHECK(beyond.error.code() == flowq::error_code::protocol_error);
}

TEST_CASE("stream receive state rejects invalid RESET_STREAM final sizes") {
    flowq::quic::stream_receive_state delivered{};
    REQUIRE(delivered.receive(stream(0, 0, "hello")).ok());

    auto too_small = delivered.reset(flowq::quic::reset_stream_frame{0, 0x2a, 4});

    CHECK_FALSE(too_small.ok());
    CHECK(too_small.error.code() == flowq::error_code::protocol_error);

    flowq::quic::stream_receive_state pending{};
    REQUIRE(pending.receive(stream(0, 5, "!")).ok());

    auto below_pending = pending.reset(flowq::quic::reset_stream_frame{0, 0x2a, 5});

    CHECK_FALSE(below_pending.ok());
    CHECK(below_pending.error.code() == flowq::error_code::protocol_error);

    flowq::quic::stream_receive_state finished{};
    REQUIRE(finished.receive(stream(0, 0, "done", true)).ok());

    auto conflict = finished.reset(flowq::quic::reset_stream_frame{0, 0x2a, 5});

    CHECK_FALSE(conflict.ok());
    CHECK(conflict.error.code() == flowq::error_code::protocol_error);
}

TEST_CASE("stream receive state accepts data after receive credit increases") {
    flowq::quic::stream_receive_state state{2};

    REQUIRE(state.receive(stream(0, 0, "he")).ok());
    auto blocked = state.receive(stream(0, 2, "llo"));
    state.update_max_data(5);
    auto accepted = state.receive(stream(0, 2, "llo"));

    CHECK_FALSE(blocked.ok());
    CHECK(blocked.error.code() == flowq::error_code::flow_control_error);
    REQUIRE(accepted.ok());
    CHECK(as_string(accepted.data) == "llo");
    CHECK(state.next_offset() == 5);
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

TEST_CASE("stream receive set routes stream credit updates independently") {
    flowq::quic::stream_receive_set streams{2};

    auto first_prefix = streams.receive(stream(0, 0, "he"));
    auto first_blocked = streams.receive(stream(0, 2, "llo"));
    auto second_blocked = streams.receive(stream(4, 0, "beta"));
    streams.update_max_data(0, 5);
    auto first_suffix = streams.receive(stream(0, 2, "llo"));

    REQUIRE(first_prefix.result.ok());
    CHECK_FALSE(first_blocked.result.ok());
    CHECK(first_blocked.result.error.code() == flowq::error_code::flow_control_error);
    CHECK_FALSE(second_blocked.result.ok());
    CHECK(second_blocked.result.error.code() == flowq::error_code::flow_control_error);
    REQUIRE(first_suffix.result.ok());
    CHECK(as_string(first_suffix.result.data) == "llo");
    REQUIRE(streams.find(0) != nullptr);
    REQUIRE(streams.find(4) != nullptr);
    CHECK(streams.find(0)->max_data() == 5);
    CHECK(streams.find(4)->max_data() == 2);
}

TEST_CASE("stream send state emits STREAM frames with stable offsets") {
    flowq::quic::stream_send_state state{4};
    REQUIRE(state.append(text("hello")).ok());

    auto result = state.pop_frame(16);

    REQUIRE(result.ok());
    REQUIRE(result.has_frame);
    CHECK(result.range.offset == 0);
    CHECK(result.range.length == 5);
    CHECK_FALSE(result.range.fin);
    CHECK(result.frame.stream_id == 4);
    CHECK(result.frame.offset == 0);
    CHECK_FALSE(result.frame.offset_present);
    CHECK(result.frame.length_present);
    CHECK_FALSE(result.frame.fin);
    CHECK(as_string(result.frame.data) == "hello");
}

TEST_CASE("stream send state fragments data by payload limit") {
    flowq::quic::stream_send_state state{0};
    REQUIRE(state.append(text("hello")).ok());

    auto first = state.pop_frame(2);
    auto second = state.pop_frame(2);
    auto third = state.pop_frame(2);

    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(third.ok());
    REQUIRE(first.has_frame);
    REQUIRE(second.has_frame);
    REQUIRE(third.has_frame);
    CHECK(first.frame.offset == 0);
    CHECK_FALSE(first.frame.offset_present);
    CHECK(as_string(first.frame.data) == "he");
    CHECK(second.frame.offset == 2);
    CHECK(second.frame.offset_present);
    CHECK(as_string(second.frame.data) == "ll");
    CHECK(third.frame.offset == 4);
    CHECK(third.frame.offset_present);
    CHECK(as_string(third.frame.data) == "o");
}

TEST_CASE("stream send state attaches FIN to the final data frame") {
    flowq::quic::stream_send_state state{0};
    REQUIRE(state.append(text("done")).ok());
    REQUIRE(state.finish().ok());

    auto result = state.pop_frame(16);

    REQUIRE(result.ok());
    REQUIRE(result.has_frame);
    CHECK(result.range.offset == 0);
    CHECK(result.range.length == 4);
    CHECK(result.range.fin);
    CHECK(result.frame.fin);
    CHECK(as_string(result.frame.data) == "done");
}

TEST_CASE("stream send state emits empty FIN frames") {
    flowq::quic::stream_send_state state{0};
    REQUIRE(state.finish().ok());

    auto result = state.pop_frame(0);

    REQUIRE(result.ok());
    REQUIRE(result.has_frame);
    CHECK(result.range.offset == 0);
    CHECK(result.range.length == 0);
    CHECK(result.range.fin);
    CHECK(result.frame.offset == 0);
    CHECK_FALSE(result.frame.offset_present);
    CHECK(result.frame.fin);
    CHECK(result.frame.data.empty());
}

TEST_CASE("stream send state rejects append after finish") {
    flowq::quic::stream_send_state state{0};
    REQUIRE(state.finish().ok());

    auto result = state.append(text("late"));

    CHECK_FALSE(result.ok());
    CHECK(result.error.code() == flowq::error_code::protocol_error);
}

TEST_CASE("stream send state retransmits lost data with identical bytes") {
    flowq::quic::stream_send_state state{0};
    REQUIRE(state.append(text("hello")).ok());
    auto sent = state.pop_frame(5);
    REQUIRE(sent.ok());
    REQUIRE(sent.has_frame);

    state.on_lost(sent.range);
    auto retransmit = state.pop_frame(5);

    REQUIRE(retransmit.ok());
    REQUIRE(retransmit.has_frame);
    CHECK(retransmit.frame.offset == 0);
    CHECK(as_string(retransmit.frame.data) == "hello");
}

TEST_CASE("stream send state does not emit lost data when payload limit is zero") {
    flowq::quic::stream_send_state state{0};
    REQUIRE(state.append(text("hello")).ok());
    auto sent = state.pop_frame(5);
    REQUIRE(sent.ok());
    REQUIRE(sent.has_frame);

    state.on_lost(sent.range);
    auto blocked = state.pop_frame(0);
    auto retransmit = state.pop_frame(5);

    REQUIRE(blocked.ok());
    CHECK_FALSE(blocked.has_frame);
    REQUIRE(retransmit.ok());
    REQUIRE(retransmit.has_frame);
    CHECK(as_string(retransmit.frame.data) == "hello");
}

TEST_CASE("stream send state suppresses retransmission after ACK") {
    flowq::quic::stream_send_state state{0};
    REQUIRE(state.append(text("hello")).ok());
    auto sent = state.pop_frame(5);
    REQUIRE(sent.ok());
    REQUIRE(sent.has_frame);

    state.on_acked(sent.range);
    state.on_lost(sent.range);
    auto result = state.pop_frame(5);

    REQUIRE(result.ok());
    CHECK_FALSE(result.has_frame);
}

TEST_CASE("stream send state drops queued lost data after ACK") {
    flowq::quic::stream_send_state state{0};
    REQUIRE(state.append(text("hello")).ok());
    auto sent = state.pop_frame(5);
    REQUIRE(sent.ok());
    REQUIRE(sent.has_frame);

    state.on_lost(sent.range);
    state.on_acked(sent.range);
    auto result = state.pop_frame(5);

    REQUIRE(result.ok());
    CHECK_FALSE(result.has_frame);
}

TEST_CASE("stream send state suppresses FIN retransmission after late ACK") {
    flowq::quic::stream_send_state state{0};
    REQUIRE(state.finish().ok());
    auto sent = state.pop_frame(0);
    REQUIRE(sent.ok());
    REQUIRE(sent.has_frame);
    REQUIRE(sent.range.fin);

    state.on_lost(sent.range);
    state.on_acked(sent.range);
    auto result = state.pop_frame(0);

    REQUIRE(result.ok());
    CHECK_FALSE(result.has_frame);
    CHECK(state.closed());
}

TEST_CASE("stream send state suppresses FIN retransmission after partial lost data retransmission late ACK") {
    flowq::quic::stream_send_state state{0};
    REQUIRE(state.append(text("hello")).ok());
    REQUIRE(state.finish().ok());
    auto sent = state.pop_frame(5);
    REQUIRE(sent.ok());
    REQUIRE(sent.has_frame);
    REQUIRE(sent.range.fin);

    state.on_lost(sent.range);
    auto partial = state.pop_frame(2);
    REQUIRE(partial.ok());
    REQUIRE(partial.has_frame);
    CHECK(as_string(partial.frame.data) == "he");
    CHECK_FALSE(partial.range.fin);

    state.on_acked(sent.range);
    auto result = state.pop_frame(5);

    REQUIRE(result.ok());
    CHECK_FALSE(result.has_frame);
    CHECK(state.closed());
}

TEST_CASE("stream send state ignores loss for unsent empty FIN") {
    flowq::quic::stream_send_state state{0};
    REQUIRE(state.finish().ok());

    state.on_lost(flowq::quic::stream_send_range{0, 0, true});
    auto result = state.pop_frame(0);

    REQUIRE(result.ok());
    REQUIRE(result.has_frame);
    CHECK(result.range.fin);
    CHECK_FALSE(result.retransmission);
}

TEST_CASE("stream send state drops queued lost fragments covered by a larger ACK") {
    flowq::quic::stream_send_state state{0};
    REQUIRE(state.append(text("hello")).ok());
    auto sent = state.pop_frame(5);
    REQUIRE(sent.ok());
    REQUIRE(sent.has_frame);

    state.on_lost(sent.range);
    auto partial = state.pop_frame(2);
    REQUIRE(partial.ok());
    REQUIRE(partial.has_frame);
    CHECK(as_string(partial.frame.data) == "he");

    state.on_acked(sent.range);
    auto result = state.pop_frame(5);

    REQUIRE(result.ok());
    CHECK_FALSE(result.has_frame);
}

TEST_CASE("stream send state limits new data by peer stream credit") {
    flowq::quic::stream_send_state state{0, 2};
    REQUIRE(state.append(text("hello")).ok());

    auto first = state.pop_frame(16);
    auto blocked = state.pop_frame(16);

    REQUIRE(first.ok());
    REQUIRE(first.has_frame);
    CHECK(as_string(first.frame.data) == "he");
    CHECK(first.range.offset == 0);
    CHECK(first.range.length == 2);
    REQUIRE(blocked.ok());
    CHECK_FALSE(blocked.has_frame);
    CHECK(state.blocked());
}

TEST_CASE("stream send state emits pending data after peer credit increases") {
    flowq::quic::stream_send_state state{0, 2};
    REQUIRE(state.append(text("hello")).ok());
    auto first = state.pop_frame(16);
    REQUIRE(first.ok());
    REQUIRE(first.has_frame);

    state.update_max_data(5);
    auto second = state.pop_frame(16);

    REQUIRE(second.ok());
    REQUIRE(second.has_frame);
    CHECK(second.range.offset == 2);
    CHECK(as_string(second.frame.data) == "llo");
    CHECK_FALSE(state.blocked());
}

TEST_CASE("stream send state applies MAX_STREAM_DATA frame credit") {
    flowq::quic::stream_send_state state{0, 2};
    REQUIRE(state.append(text("hello")).ok());
    auto prefix = state.pop_frame(16);
    REQUIRE(prefix.ok());
    REQUIRE(prefix.has_frame);
    REQUIRE(state.blocked());

    state.update_max_data(flowq::quic::max_stream_data_frame{0, 5});
    auto suffix = state.pop_frame(16);

    REQUIRE(suffix.ok());
    REQUIRE(suffix.has_frame);
    CHECK(suffix.range.offset == 2);
    CHECK(as_string(suffix.frame.data) == "llo");
    CHECK_FALSE(state.blocked());
}

TEST_CASE("stream send state ignores stale and mismatched MAX_STREAM_DATA frames") {
    flowq::quic::stream_send_state state{0, 2};
    REQUIRE(state.append(text("hello")).ok());
    auto prefix = state.pop_frame(16);
    REQUIRE(prefix.ok());
    REQUIRE(prefix.has_frame);

    state.update_max_data(flowq::quic::max_stream_data_frame{4, 5});
    CHECK(state.blocked());
    state.update_max_data(flowq::quic::max_stream_data_frame{0, 5});
    state.update_max_data(flowq::quic::max_stream_data_frame{0, 2});
    auto suffix = state.pop_frame(16);

    REQUIRE(suffix.ok());
    REQUIRE(suffix.has_frame);
    CHECK(as_string(suffix.frame.data) == "llo");
}

TEST_CASE("stream send state delays FIN until final data is within credit") {
    flowq::quic::stream_send_state state{0, 2};
    REQUIRE(state.append(text("hello")).ok());
    REQUIRE(state.finish().ok());

    auto first = state.pop_frame(16);
    auto blocked = state.pop_frame(16);
    state.update_max_data(5);
    auto final = state.pop_frame(16);

    REQUIRE(first.ok());
    REQUIRE(first.has_frame);
    CHECK(as_string(first.frame.data) == "he");
    CHECK_FALSE(first.frame.fin);
    REQUIRE(blocked.ok());
    CHECK_FALSE(blocked.has_frame);
    REQUIRE(final.ok());
    REQUIRE(final.has_frame);
    CHECK(final.range.offset == 2);
    CHECK(as_string(final.frame.data) == "llo");
    CHECK(final.range.fin);
    CHECK(final.frame.fin);
}

TEST_CASE("stream send state does not emit empty FIN before buffered data") {
    flowq::quic::stream_send_state state{0};
    REQUIRE(state.append(text("hello")).ok());
    REQUIRE(state.finish().ok());

    auto premature = state.pop_frame(0);
    state.on_acked(flowq::quic::stream_send_range{5, 0, true});

    REQUIRE(premature.ok());
    CHECK_FALSE(premature.has_frame);
    CHECK_FALSE(state.fin_sent());
    CHECK_FALSE(state.fin_acked());
    CHECK_FALSE(state.closed());

    auto final = state.pop_frame(5);
    REQUIRE(final.ok());
    REQUIRE(final.has_frame);
    CHECK(as_string(final.frame.data) == "hello");
    CHECK(final.frame.fin);
}

TEST_CASE("stream send state reports lifecycle through FIN acknowledgement") {
    flowq::quic::stream_send_state state{0};

    CHECK_FALSE(state.finished());
    CHECK_FALSE(state.fin_sent());
    CHECK_FALSE(state.fin_acked());
    CHECK_FALSE(state.closed());

    REQUIRE(state.finish().ok());
    CHECK(state.finished());
    CHECK_FALSE(state.fin_sent());
    CHECK_FALSE(state.fin_acked());
    CHECK_FALSE(state.closed());

    auto fin = state.pop_frame(0);
    REQUIRE(fin.ok());
    REQUIRE(fin.has_frame);
    CHECK(state.fin_sent());
    CHECK_FALSE(state.fin_acked());
    CHECK_FALSE(state.closed());

    state.on_acked(fin.range);
    CHECK(state.finished());
    CHECK(state.fin_sent());
    CHECK(state.fin_acked());
    CHECK(state.closed());
}

TEST_CASE("stream send state retransmits lost sent data while new data is credit blocked") {
    flowq::quic::stream_send_state state{0, 2};
    REQUIRE(state.append(text("hello")).ok());
    auto sent = state.pop_frame(16);
    REQUIRE(sent.ok());
    REQUIRE(sent.has_frame);
    REQUIRE(state.blocked());

    state.on_lost(sent.range);
    auto retransmit = state.pop_frame(16);

    REQUIRE(retransmit.ok());
    REQUIRE(retransmit.has_frame);
    CHECK(retransmit.range.offset == 0);
    CHECK(as_string(retransmit.frame.data) == "he");
    CHECK(state.blocked());
}

TEST_CASE("stream send state reports STREAM_DATA_BLOCKED frame when blocked") {
    flowq::quic::stream_send_state state{8, 2};
    REQUIRE(state.append(text("hello")).ok());
    auto prefix = state.pop_frame(16);
    REQUIRE(prefix.ok());
    REQUIRE(prefix.has_frame);

    auto blocked = state.blocked_frame();

    REQUIRE(blocked.has_value());
    CHECK(blocked->stream_id == 8);
    CHECK(blocked->maximum_stream_data == 2);
}

TEST_CASE("stream send state reports no STREAM_DATA_BLOCKED frame when unblocked") {
    flowq::quic::stream_send_state empty{8, 2};
    CHECK_FALSE(empty.blocked_frame().has_value());

    flowq::quic::stream_send_state unblocked{8, 8};
    REQUIRE(unblocked.append(text("hello")).ok());
    CHECK_FALSE(unblocked.blocked_frame().has_value());

    auto sent = unblocked.pop_frame(16);
    REQUIRE(sent.ok());
    REQUIRE(sent.has_frame);
    CHECK_FALSE(unblocked.blocked_frame().has_value());
}

TEST_CASE("stream send state applies STOP_SENDING as stopped marker") {
    flowq::quic::stream_send_state state{0};
    REQUIRE(state.append(text("hello")).ok());

    auto stopped = state.stop_sending(flowq::quic::stop_sending_frame{0, 0x2a});
    auto result = state.pop_frame(16);
    auto late_append = state.append(text("late"));

    REQUIRE(stopped.ok());
    CHECK(state.stop_requested());
    CHECK(state.stop_error_code() == 0x2a);
    REQUIRE(result.ok());
    CHECK_FALSE(result.has_frame);
    CHECK_FALSE(late_append.ok());
    CHECK(late_append.error.code() == flowq::error_code::protocol_error);
}

TEST_CASE("stream send state suppresses retransmission after STOP_SENDING") {
    flowq::quic::stream_send_state state{0};
    REQUIRE(state.append(text("hello")).ok());
    auto sent = state.pop_frame(5);
    REQUIRE(sent.ok());
    REQUIRE(sent.has_frame);
    state.on_lost(sent.range);

    REQUIRE(state.stop_sending(flowq::quic::stop_sending_frame{0, 0x2a}).ok());
    auto retransmit = state.pop_frame(5);

    REQUIRE(retransmit.ok());
    CHECK_FALSE(retransmit.has_frame);
    CHECK_FALSE(state.has_retransmittable_data());
}

TEST_CASE("stream send state retransmits lost FIN") {
    flowq::quic::stream_send_state state{0};
    REQUIRE(state.finish().ok());
    auto fin = state.pop_frame(0);
    REQUIRE(fin.ok());
    REQUIRE(fin.has_frame);

    state.on_lost(fin.range);
    auto retransmit = state.pop_frame(0);

    REQUIRE(retransmit.ok());
    REQUIRE(retransmit.has_frame);
    CHECK(retransmit.range.fin);
    CHECK(retransmit.frame.fin);
    CHECK(retransmit.frame.data.empty());

    auto after = state.pop_frame(0);
    REQUIRE(after.ok());
    CHECK_FALSE(after.has_frame);
}

TEST_CASE("stream send frames feed receive reassembly") {
    flowq::quic::stream_send_state sender{0};
    flowq::quic::stream_receive_state receiver{};
    REQUIRE(sender.append(text("hello flowq")).ok());
    REQUIRE(sender.finish().ok());

    auto first = sender.pop_frame(5);
    auto second = sender.pop_frame(16);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(first.has_frame);
    REQUIRE(second.has_frame);

    auto first_delivery = receiver.receive(first.frame);
    auto second_delivery = receiver.receive(second.frame);

    REQUIRE(first_delivery.ok());
    REQUIRE(second_delivery.ok());
    CHECK(as_string(first_delivery.data) == "hello");
    CHECK(as_string(second_delivery.data) == " flowq");
    CHECK(second_delivery.closed);
    CHECK(receiver.closed());
}

TEST_CASE("stream send set routes independent stream send states") {
    flowq::quic::stream_send_set streams{};
    REQUIRE(streams.append(0, text("alpha")).ok());
    REQUIRE(streams.append(4, text("beta")).ok());

    auto first = streams.pop_frame(0, 16);
    auto second = streams.pop_frame(4, 16);

    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(first.has_frame);
    REQUIRE(second.has_frame);
    CHECK(first.frame.stream_id == 0);
    CHECK(second.frame.stream_id == 4);
    CHECK(first.frame.offset == 0);
    CHECK(second.frame.offset == 0);
    CHECK(as_string(first.frame.data) == "alpha");
    CHECK(as_string(second.frame.data) == "beta");
}

TEST_CASE("stream send set routes peer stream credit updates independently") {
    flowq::quic::stream_send_set streams{2};
    REQUIRE(streams.append(0, text("hello")).ok());
    REQUIRE(streams.append(4, text("beta")).ok());

    auto first_prefix = streams.pop_frame(0, 16);
    auto second_prefix = streams.pop_frame(4, 16);
    auto first_blocked = streams.pop_frame(0, 16);
    streams.update_max_data(0, 5);
    auto first_suffix = streams.pop_frame(0, 16);
    auto second_blocked = streams.pop_frame(4, 16);

    REQUIRE(first_prefix.ok());
    REQUIRE(second_prefix.ok());
    REQUIRE(first_prefix.has_frame);
    REQUIRE(second_prefix.has_frame);
    CHECK(as_string(first_prefix.frame.data) == "he");
    CHECK(as_string(second_prefix.frame.data) == "be");
    REQUIRE(first_blocked.ok());
    CHECK_FALSE(first_blocked.has_frame);
    REQUIRE(first_suffix.ok());
    REQUIRE(first_suffix.has_frame);
    CHECK(first_suffix.range.offset == 2);
    CHECK(as_string(first_suffix.frame.data) == "llo");
    REQUIRE(second_blocked.ok());
    CHECK_FALSE(second_blocked.has_frame);
}

TEST_CASE("stream send set routes MAX_STREAM_DATA frames by stream id") {
    flowq::quic::stream_send_set streams{2};
    REQUIRE(streams.append(0, text("hello")).ok());
    REQUIRE(streams.append(4, text("beta")).ok());
    REQUIRE(streams.pop_frame(0, 16).has_frame);
    REQUIRE(streams.pop_frame(4, 16).has_frame);

    streams.update_max_data(flowq::quic::max_stream_data_frame{0, 5});
    auto resumed = streams.pop_frame(0, 16);
    auto still_blocked = streams.pop_frame(4, 16);

    REQUIRE(resumed.ok());
    REQUIRE(resumed.has_frame);
    CHECK(resumed.range.offset == 2);
    CHECK(as_string(resumed.frame.data) == "llo");
    REQUIRE(still_blocked.ok());
    CHECK_FALSE(still_blocked.has_frame);
}

TEST_CASE("stream receive and send sets route reset and stop by stream id") {
    flowq::quic::stream_receive_set receive_streams{};
    auto reset = receive_streams.reset(flowq::quic::reset_stream_frame{4, 0x2a, 3});

    REQUIRE(reset.result.ok());
    CHECK(reset.stream_id == 4);
    REQUIRE(receive_streams.find(4) != nullptr);
    CHECK(receive_streams.find(4)->reset_received());
    CHECK(receive_streams.find(4)->reset_final_size() == 3);
    CHECK(receive_streams.find(0) == nullptr);

    flowq::quic::stream_send_set send_streams{};
    REQUIRE(send_streams.append(0, text("alpha")).ok());
    REQUIRE(send_streams.append(4, text("beta")).ok());
    auto stopped = send_streams.stop_sending(flowq::quic::stop_sending_frame{4, 0x2a});
    auto first = send_streams.pop_frame(0, 16);
    auto second = send_streams.pop_frame(4, 16);

    REQUIRE(stopped.ok());
    REQUIRE(send_streams.find(4) != nullptr);
    CHECK(send_streams.find(4)->stop_requested());
    REQUIRE(first.ok());
    CHECK(first.has_frame);
    REQUIRE(second.ok());
    CHECK_FALSE(second.has_frame);
}

TEST_CASE("stream send set reports STREAM_DATA_BLOCKED frame for selected blocked stream") {
    flowq::quic::stream_send_set streams{2};
    REQUIRE(streams.append(0, text("hello")).ok());
    REQUIRE(streams.append(4, text("beta")).ok());
    REQUIRE(streams.pop_frame(0, 16).has_frame);
    REQUIRE(streams.pop_frame(4, 16).has_frame);

    auto first = streams.blocked_frame(0);
    auto second = streams.blocked_frame(4);
    auto absent = streams.blocked_frame(8);

    REQUIRE(first.has_value());
    CHECK(first->stream_id == 0);
    CHECK(first->maximum_stream_data == 2);
    REQUIRE(second.has_value());
    CHECK(second->stream_id == 4);
    CHECK(second->maximum_stream_data == 2);
    CHECK_FALSE(absent.has_value());
}

TEST_CASE("stream send set batches STREAM frames in selected order") {
    flowq::quic::stream_send_set streams{};
    REQUIRE(streams.append(0, text("alpha")).ok());
    REQUIRE(streams.append(4, text("beta")).ok());
    const std::vector<std::uint64_t> order{4, 0};

    auto batch = streams.pop_frames(order, 4, 16);

    REQUIRE(batch.ok());
    REQUIRE(batch.frames.size() == 2);
    REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(batch.frames[0]));
    REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(batch.frames[1]));
    const auto& first = std::get<flowq::quic::stream_frame>(batch.frames[0]);
    const auto& second = std::get<flowq::quic::stream_frame>(batch.frames[1]);
    CHECK(first.stream_id == 4);
    CHECK(as_string(first.data) == "beta");
    CHECK(second.stream_id == 0);
    CHECK(as_string(second.data) == "alpha");
}

TEST_CASE("stream send set respects scheduled frame limit") {
    flowq::quic::stream_send_set streams{};
    REQUIRE(streams.append(0, text("alpha")).ok());
    REQUIRE(streams.append(4, text("beta")).ok());
    const std::vector<std::uint64_t> order{0, 4};

    auto first_batch = streams.pop_frames(order, 1, 16);
    auto second_batch = streams.pop_frames(order, 1, 16);

    REQUIRE(first_batch.ok());
    REQUIRE(second_batch.ok());
    REQUIRE(first_batch.frames.size() == 1);
    REQUIRE(second_batch.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(first_batch.frames[0]));
    REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(second_batch.frames[0]));
    const auto& first = std::get<flowq::quic::stream_frame>(first_batch.frames[0]);
    const auto& second = std::get<flowq::quic::stream_frame>(second_batch.frames[0]);
    CHECK(first.stream_id == 0);
    CHECK(second.stream_id == 4);
}

TEST_CASE("stream send set batches STREAM_DATA_BLOCKED for selected blocked stream") {
    flowq::quic::stream_send_set streams{2};
    REQUIRE(streams.append(0, text("hello")).ok());
    const std::vector<std::uint64_t> order{0};
    auto prefix = streams.pop_frames(order, 1, 16);
    REQUIRE(prefix.ok());
    REQUIRE(prefix.frames.size() == 1);

    auto blocked = streams.pop_frames(order, 1, 16);

    REQUIRE(blocked.ok());
    REQUIRE(blocked.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::stream_data_blocked_frame>(blocked.frames[0]));
    const auto& blocked_frame = std::get<flowq::quic::stream_data_blocked_frame>(blocked.frames[0]);
    CHECK(blocked_frame.stream_id == 0);
    CHECK(blocked_frame.maximum_stream_data == 2);
}

TEST_CASE("stream send set emits STREAM after credit update instead of blocked frame") {
    flowq::quic::stream_send_set streams{2};
    REQUIRE(streams.append(0, text("hello")).ok());
    const std::vector<std::uint64_t> order{0};
    REQUIRE(streams.pop_frames(order, 1, 16).frames.size() == 1);
    REQUIRE(streams.pop_frames(order, 1, 16).frames.size() == 1);

    streams.update_max_data(flowq::quic::max_stream_data_frame{0, 5});
    auto resumed = streams.pop_frames(order, 1, 16);

    REQUIRE(resumed.ok());
    REQUIRE(resumed.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(resumed.frames[0]));
    const auto& suffix = std::get<flowq::quic::stream_frame>(resumed.frames[0]);
    CHECK(suffix.stream_id == 0);
    CHECK(suffix.offset == 2);
    CHECK(as_string(suffix.data) == "llo");
}

TEST_CASE("stream send set returns empty successful schedule when no frame is available") {
    flowq::quic::stream_send_set streams{};
    const std::vector<std::uint64_t> empty_order{};
    const std::vector<std::uint64_t> absent_order{8};

    auto no_streams = streams.pop_frames(empty_order, 4, 16);
    auto no_frames_allowed = streams.pop_frames(absent_order, 0, 16);
    auto absent = streams.pop_frames(absent_order, 4, 16);

    REQUIRE(no_streams.ok());
    REQUIRE(no_frames_allowed.ok());
    REQUIRE(absent.ok());
    CHECK(no_streams.frames.empty());
    CHECK(no_frames_allowed.frames.empty());
    CHECK(absent.frames.empty());
}
