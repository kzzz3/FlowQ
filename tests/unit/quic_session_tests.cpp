#include <flowq/quic/session.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using clock_type = std::chrono::steady_clock;

clock_type::time_point at(std::chrono::milliseconds offset) {
    return clock_type::time_point{offset};
}

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

flowq::quic::session_config make_config(
    flowq::quic::connection_id local,
    flowq::quic::connection_id remote,
    flowq::endpoint peer,
    const flowq::quic::packet_protector& protector) {
    flowq::quic::session_config config{};
    config.role = flowq::quic::connection_role::client;
    config.local_connection_id = std::move(local);
    config.remote_connection_id = std::move(remote);
    config.peer = std::move(peer);
    config.initial_protector = &protector;
    config.handshake_protector = &protector;
    config.application_protector = &protector;
    return config;
}

} // namespace

TEST_CASE("QUIC session public header exposes basic client configuration") {
    flowq::quic::session_config config{};
    config.role = flowq::quic::connection_role::client;

    CHECK(config.version == 1);
    CHECK(config.protection_policy == flowq::quic::packet_protection_policy::test_allowed);
    CHECK_FALSE(config.disable_active_migration);
    CHECK(config.active_connection_id_limit == 2);
}

TEST_CASE("QUIC session queues stream data before flush returns outbound Application datagrams") {
    flowq::quic::plaintext_packet_protector protector{};
    auto session = flowq::quic::session{make_config(
        cid({0x01}),
        cid({0x02}),
        flowq::endpoint{"server", 4433, "hq-interop"},
        protector)};

    REQUIRE(session.append_stream_data(0, text("hello")).ok());

    auto queued = session.queue_stream_data({0});
    REQUIRE(queued.ok());
    CHECK(queued.datagrams.empty());

    auto result = session.flush(at(0ms));

    REQUIRE(result.ok());
    REQUIRE(result.datagrams.size() == 1);
    CHECK(result.datagrams[0].peer.host == "server");
    CHECK(result.datagrams[0].peer.port == 4433);
    CHECK_FALSE(result.datagrams[0].payload.empty());
}

TEST_CASE("QUIC session receives Application datagrams as stream deliveries") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = flowq::quic::session{make_config(
        cid({0x01}),
        cid({0x02}),
        flowq::endpoint{"server", 4433, "hq-interop"},
        protector)};
    auto server = flowq::quic::session{make_config(
        cid({0x02}),
        cid({0x01}),
        flowq::endpoint{"client", 1111, "hq-interop"},
        protector)};

    REQUIRE(client.append_stream_data(0, text("hello")).ok());
    auto queued = client.queue_stream_data({0});
    REQUIRE(queued.ok());
    CHECK(queued.datagrams.empty());

    auto sent = client.flush(at(0ms));
    REQUIRE(sent.ok());
    REQUIRE(sent.datagrams.size() == 1);

    auto received = server.on_datagram(flowq::quic::inbound_datagram{std::move(sent.datagrams[0].payload), sent.datagrams[0].peer});

    REQUIRE(received.ok());
    REQUIRE(received.stream_deliveries.size() == 1);
    REQUIRE(received.stream_deliveries[0].ok());
    CHECK(received.stream_deliveries[0].stream_id == 0);
    CHECK(as_string(received.stream_deliveries[0].data) == "hello");
}

TEST_CASE("QUIC session acknowledges received Application datagrams") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = flowq::quic::session{make_config(
        cid({0x01}),
        cid({0x02}),
        flowq::endpoint{"server", 4433, "hq-interop"},
        protector)};
    auto server = flowq::quic::session{make_config(
        cid({0x02}),
        cid({0x01}),
        flowq::endpoint{"client", 1111, "hq-interop"},
        protector)};

    REQUIRE(client.append_stream_data(0, text("hello")).ok());
    REQUIRE(client.queue_stream_data({0}).ok());
    auto sent = client.flush(at(0ms));
    REQUIRE(sent.ok());
    REQUIRE(sent.datagrams.size() == 1);
    REQUIRE(server.on_datagram(flowq::quic::inbound_datagram{std::move(sent.datagrams[0].payload), sent.datagrams[0].peer}).ok());

    auto acked = server.acknowledge(flowq::quic::packet_number_space::application);

    REQUIRE(acked.ok());
    REQUIRE(acked.datagrams.size() == 1);
    CHECK(acked.datagrams[0].peer.host == "client");
}

TEST_CASE("QUIC session does not arm Application recovery timer before handshake confirmation") {
    flowq::quic::plaintext_packet_protector protector{};
    auto session = flowq::quic::session{make_config(
        cid({0x01}),
        cid({0x02}),
        flowq::endpoint{"server", 4433, "hq-interop"},
        protector)};

    REQUIRE(session.append_stream_data(0, text("hello")).ok());
    REQUIRE(session.queue_stream_data({0}).ok());
    REQUIRE(session.flush(at(0ms)).ok());

    auto timer = session.next_recovery_timer(at(0ms));

    CHECK_FALSE(timer.has_value());
}
