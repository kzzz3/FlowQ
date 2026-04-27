#include <flowq/quic/connection.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <initializer_list>
#include <span>
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

flowq::quic::connection_id cid(std::initializer_list<unsigned int> values) {
    return flowq::quic::connection_id{flowq::buffer{bytes(values)}};
}

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

flowq::quic::connection_loop make_loop(
    flowq::quic::connection_id local,
    flowq::quic::connection_id remote,
    flowq::endpoint peer,
    const flowq::quic::packet_protector& protector,
    std::uint64_t initial_stream_send_max_data = UINT64_MAX,
    std::uint64_t initial_connection_send_max_data = UINT64_MAX) {
    return flowq::quic::connection_loop{flowq::quic::connection_loop_config{
        flowq::quic::connection_role::client,
        1,
        std::move(local),
        std::move(remote),
        std::move(peer),
        &protector,
        &protector,
        flowq::quic::packet_pipeline_config{1200},
        initial_stream_send_max_data,
        initial_connection_send_max_data
    }};
}

flowq::quic::outbound_datagram require_single_outbound(std::vector<flowq::quic::connection_loop_action> actions) {
    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::outbound_datagram>(actions[0]));
    return std::get<flowq::quic::outbound_datagram>(std::move(actions[0]));
}

} // namespace

TEST_CASE("connection loop flushes queued Initial frames as an outbound datagram") {
    flowq::quic::plaintext_packet_protector protector{};
    flowq::endpoint peer{"127.0.0.1", 4433, "hq-interop"};
    auto loop = make_loop(cid({0x01}), cid({0x02}), peer, protector);

    loop.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.flush();

    auto datagram = require_single_outbound(loop.drain_actions());
    CHECK(datagram.peer.host == "127.0.0.1");
    CHECK(datagram.peer.port == 4433);
    CHECK_FALSE(datagram.payload.empty());
    CHECK(loop.sent_packets(flowq::quic::packet_number_space::initial).packets().size() == 1);
    CHECK(loop.sent_packets(flowq::quic::packet_number_space::initial).packets()[0].packet_number == 0);
}

TEST_CASE("connection loop parses inbound Initial packets and generates ACK packets") {
    flowq::quic::plaintext_packet_protector protector{};
    flowq::endpoint client_peer{"client", 1111, "hq-interop"};
    flowq::endpoint server_peer{"server", 4433, "hq-interop"};
    auto client = make_loop(cid({0x01}), cid({0x02}), server_peer, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), client_peer, protector);

    client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.flush();
    auto datagram = require_single_outbound(client.drain_actions());

    server.on_datagram(flowq::quic::inbound_datagram{std::move(datagram.payload), datagram.peer});
    auto received_actions = server.drain_actions();
    REQUIRE(received_actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::received_packet_event>(received_actions[0]));
    const auto& received = std::get<flowq::quic::received_packet_event>(received_actions[0]);
    CHECK(received.number.space == flowq::quic::packet_number_space::initial);
    CHECK(received.number.value == 0);
    REQUIRE(received.frames.size() == 1);
    CHECK(std::holds_alternative<flowq::quic::ping_frame>(received.frames[0]));

    server.acknowledge(flowq::quic::packet_number_space::initial);
    auto ack_datagram = require_single_outbound(server.drain_actions());
    auto parsed_ack = flowq::quic::parse_long_packet(ack_datagram.payload, protector);
    REQUIRE(parsed_ack.ok());
    REQUIRE(parsed_ack.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::ack_frame>(parsed_ack.frames[0]));
    CHECK(std::get<flowq::quic::ack_frame>(parsed_ack.frames[0]).largest_acknowledged == 0);
}

TEST_CASE("connection loop applies inbound ACK frames to sent packet trackers") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);

    client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.flush();
    auto outbound = require_single_outbound(client.drain_actions());

    server.on_datagram(flowq::quic::inbound_datagram{std::move(outbound.payload), outbound.peer});
    (void)server.drain_actions();
    server.acknowledge(flowq::quic::packet_number_space::initial);
    auto ack_datagram = require_single_outbound(server.drain_actions());

    client.on_datagram(flowq::quic::inbound_datagram{std::move(ack_datagram.payload), ack_datagram.peer});
    const auto& packets = client.sent_packets(flowq::quic::packet_number_space::initial).packets();
    REQUIRE(packets.size() == 1);
    CHECK(packets[0].state == flowq::quic::sent_packet_state::acknowledged);
}

TEST_CASE("connection loop routes inbound STREAM frames to receive streams") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);

    client.queue_initial({flowq::quic::frame{flowq::quic::stream_frame{0, 0, false, true, false, text("hello")}}});
    client.flush();
    auto outbound = require_single_outbound(client.drain_actions());

    server.on_datagram(flowq::quic::inbound_datagram{std::move(outbound.payload), outbound.peer});
    auto actions = server.drain_actions();

    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::received_packet_event>(actions[0]));
    const auto& event = std::get<flowq::quic::received_packet_event>(actions[0]);
    REQUIRE(event.stream_deliveries.size() == 1);
    CHECK(event.stream_deliveries[0].stream_id == 0);
    REQUIRE(event.stream_deliveries[0].result.ok());
    CHECK(as_string(event.stream_deliveries[0].result.data) == "hello");
}

TEST_CASE("connection loop schedules outbound STREAM frames from connection stream state") {
    flowq::quic::plaintext_packet_protector protector{};
    auto loop = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    const std::vector<std::uint64_t> order{0};

    REQUIRE(loop.append_stream_data(0, text("hello")).ok());
    auto scheduled = loop.schedule_stream_frames(order, 4, 16);

    REQUIRE(scheduled.ok());
    REQUIRE(scheduled.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(scheduled.frames[0]));
    const auto& stream = std::get<flowq::quic::stream_frame>(scheduled.frames[0]);
    CHECK(stream.stream_id == 0);
    CHECK(as_string(stream.data) == "hello");
}

TEST_CASE("connection loop applies inbound MAX_STREAM_DATA to stream send state") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector, 2);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);
    const std::vector<std::uint64_t> order{0};
    REQUIRE(client.append_stream_data(0, text("hello")).ok());
    auto prefix = client.schedule_stream_frames(order, 1, 16);
    REQUIRE(prefix.ok());
    REQUIRE(prefix.frames.size() == 1);
    CHECK(as_string(std::get<flowq::quic::stream_frame>(prefix.frames[0]).data) == "he");

    server.queue_initial({flowq::quic::frame{flowq::quic::max_stream_data_frame{0, 5}}});
    server.flush();
    auto credit = require_single_outbound(server.drain_actions());
    client.on_datagram(flowq::quic::inbound_datagram{std::move(credit.payload), credit.peer});
    (void)client.drain_actions();
    auto suffix = client.schedule_stream_frames(order, 1, 16);

    REQUIRE(suffix.ok());
    REQUIRE(suffix.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(suffix.frames[0]));
    const auto& stream = std::get<flowq::quic::stream_frame>(suffix.frames[0]);
    CHECK(stream.offset == 2);
    CHECK(as_string(stream.data) == "llo");
}

TEST_CASE("connection loop applies MAX_DATA to aggregate stream send credit") {
    flowq::quic::plaintext_packet_protector protector{};
    auto loop = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector, UINT64_MAX, 2);
    const std::vector<std::uint64_t> order{0};
    REQUIRE(loop.append_stream_data(0, text("hello")).ok());

    auto prefix = loop.schedule_stream_frames(order, 4, 16);
    loop.update_max_data(flowq::quic::max_data_frame{1});
    auto stale = loop.schedule_stream_frames(order, 4, 16);
    loop.update_max_data(flowq::quic::max_data_frame{5});
    auto suffix = loop.schedule_stream_frames(order, 4, 16);

    REQUIRE(prefix.ok());
    REQUIRE(prefix.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(prefix.frames[0]));
    const auto& first = std::get<flowq::quic::stream_frame>(prefix.frames[0]);
    CHECK(first.offset == 0);
    CHECK(as_string(first.data) == "he");
    REQUIRE(stale.ok());
    REQUIRE(stale.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::data_blocked_frame>(stale.frames[0]));
    CHECK(std::get<flowq::quic::data_blocked_frame>(stale.frames[0]).maximum_data == 2);
    REQUIRE(suffix.ok());
    REQUIRE(suffix.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(suffix.frames[0]));
    const auto& second = std::get<flowq::quic::stream_frame>(suffix.frames[0]);
    CHECK(second.offset == 2);
    CHECK(as_string(second.data) == "llo");
}

TEST_CASE("connection loop applies inbound MAX_DATA frames to aggregate stream send credit") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector, UINT64_MAX, 2);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);
    const std::vector<std::uint64_t> order{0};
    REQUIRE(client.append_stream_data(0, text("hello")).ok());
    REQUIRE(client.schedule_stream_frames(order, 1, 16).frames.size() == 1);

    server.queue_initial({flowq::quic::frame{flowq::quic::max_data_frame{5}}});
    server.flush();
    auto credit = require_single_outbound(server.drain_actions());
    client.on_datagram(flowq::quic::inbound_datagram{std::move(credit.payload), credit.peer});
    (void)client.drain_actions();
    auto suffix = client.schedule_stream_frames(order, 1, 16);

    REQUIRE(suffix.ok());
    REQUIRE(suffix.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(suffix.frames[0]));
    const auto& stream = std::get<flowq::quic::stream_frame>(suffix.frames[0]);
    CHECK(stream.offset == 2);
    CHECK(as_string(stream.data) == "llo");
}

TEST_CASE("connection loop emits DATA_BLOCKED when aggregate stream credit is exhausted") {
    flowq::quic::plaintext_packet_protector protector{};
    auto loop = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector, UINT64_MAX, 2);
    const std::vector<std::uint64_t> order{0, 4};
    REQUIRE(loop.append_stream_data(0, text("hello")).ok());
    REQUIRE(loop.append_stream_data(4, text("beta")).ok());
    REQUIRE(loop.schedule_stream_frames(order, 1, 16).frames.size() == 1);

    auto blocked = loop.schedule_stream_frames(order, 1, 16);

    REQUIRE(blocked.ok());
    REQUIRE(blocked.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::data_blocked_frame>(blocked.frames[0]));
    CHECK(std::get<flowq::quic::data_blocked_frame>(blocked.frames[0]).maximum_data == 2);
}

TEST_CASE("connection loop preserves STREAM_DATA_BLOCKED when stream credit is exhausted first") {
    flowq::quic::plaintext_packet_protector protector{};
    auto loop = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector, 2, 5);
    const std::vector<std::uint64_t> order{0};
    REQUIRE(loop.append_stream_data(0, text("hello")).ok());
    REQUIRE(loop.schedule_stream_frames(order, 1, 16).frames.size() == 1);

    auto blocked = loop.schedule_stream_frames(order, 1, 16);

    REQUIRE(blocked.ok());
    REQUIRE(blocked.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::stream_data_blocked_frame>(blocked.frames[0]));
    const auto& frame = std::get<flowq::quic::stream_data_blocked_frame>(blocked.frames[0]);
    CHECK(frame.stream_id == 0);
    CHECK(frame.maximum_stream_data == 2);
}

TEST_CASE("connection loop ignores duplicate received packet frames") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);

    client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.flush();
    auto outbound = require_single_outbound(client.drain_actions());
    const auto payload = outbound.payload;

    server.on_datagram(flowq::quic::inbound_datagram{payload, flowq::endpoint{"client", 1111, "hq-interop"}});
    REQUIRE(server.drain_actions().size() == 1);

    server.on_datagram(flowq::quic::inbound_datagram{payload, flowq::endpoint{"client", 1111, "hq-interop"}});
    CHECK(server.drain_actions().empty());
}

TEST_CASE("connection loop keeps Initial and Handshake packet numbers independent") {
    flowq::quic::plaintext_packet_protector protector{};
    auto loop = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);

    loop.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.queue_handshake({flowq::quic::frame{flowq::quic::crypto_frame{0, flowq::buffer{bytes({0xaa})}}}});
    loop.flush();

    auto actions = loop.drain_actions();
    REQUIRE(actions.size() == 2);
    CHECK(loop.sent_packets(flowq::quic::packet_number_space::initial).packets()[0].packet_number == 0);
    CHECK(loop.sent_packets(flowq::quic::packet_number_space::handshake).packets()[0].packet_number == 0);
}

TEST_CASE("connection loop parses and acknowledges Handshake packets") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);

    client.queue_handshake({flowq::quic::frame{flowq::quic::crypto_frame{0, flowq::buffer{bytes({0xaa})}}}});
    client.flush();
    auto datagram = require_single_outbound(client.drain_actions());

    server.on_datagram(flowq::quic::inbound_datagram{std::move(datagram.payload), flowq::endpoint{"client", 1111, "hq-interop"}});
    auto received_actions = server.drain_actions();
    REQUIRE(received_actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::received_packet_event>(received_actions[0]));
    const auto& received = std::get<flowq::quic::received_packet_event>(received_actions[0]);
    CHECK(received.peer.host == "client");
    CHECK(received.number.space == flowq::quic::packet_number_space::handshake);
    CHECK(received.number.value == 0);
    REQUIRE(received.frames.size() == 1);
    CHECK(std::holds_alternative<flowq::quic::crypto_frame>(received.frames[0]));

    server.acknowledge(flowq::quic::packet_number_space::handshake);
    auto ack_datagram = require_single_outbound(server.drain_actions());
    auto parsed_ack = flowq::quic::parse_long_packet(ack_datagram.payload, protector);
    REQUIRE(parsed_ack.ok());
    CHECK(parsed_ack.space == flowq::quic::packet_number_space::handshake);
    REQUIRE(parsed_ack.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::ack_frame>(parsed_ack.frames[0]));
    CHECK(std::get<flowq::quic::ack_frame>(parsed_ack.frames[0]).largest_acknowledged == 0);
}

TEST_CASE("connection loop rejects unsupported application packet number space") {
    flowq::quic::plaintext_packet_protector protector{};
    auto loop = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);

    CHECK_THROWS_AS((void)loop.sent_packets(flowq::quic::packet_number_space::application), std::invalid_argument);

    loop.acknowledge(flowq::quic::packet_number_space::application);
    auto actions = loop.drain_actions();
    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::close_action>(actions[0]));
    CHECK_FALSE(std::get<flowq::quic::close_action>(actions[0]).error.ok());
}

TEST_CASE("connection loop converts invalid inbound datagrams to close actions") {
    flowq::quic::plaintext_packet_protector protector{};
    auto loop = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);

    loop.on_datagram(flowq::quic::inbound_datagram{flowq::buffer{bytes({0xc0})}, flowq::endpoint{"peer", 9999, ""}});

    auto actions = loop.drain_actions();
    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::close_action>(actions[0]));
    CHECK_FALSE(std::get<flowq::quic::close_action>(actions[0]).error.ok());
}
