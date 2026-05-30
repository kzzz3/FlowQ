#include <flowq/quic/connection.hpp>
#include <flowq/quic/tls_handshake.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <initializer_list>
#include <span>
#include <string>
#include <variant>
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

class recording_tls_adapter final : public flowq::quic::tls_handshake_adapter {
public:
    flowq::quic::handshake_state state() const noexcept override {
        return state_value;
    }

    flowq::quic::tls_key_availability key_availability() const noexcept override {
        return keys;
    }

    flowq::quic::crypto_provider_status provider_status() const noexcept override {
        return status;
    }

    flowq::error receive_crypto(flowq::quic::crypto_bytes bytes) override {
        received.push_back(std::move(bytes));
        if (install_handshake_keys_on_receive) {
            state_value = flowq::quic::handshake_state::handshaking;
            keys.handshake = true;
        }
        if (confirm_handshake_on_receive) {
            state_value = flowq::quic::handshake_state::handshake_confirmed;
            keys.handshake = true;
            keys.application = true;
        }
        return {};
    }

    std::vector<flowq::quic::crypto_bytes> drain_crypto() override {
        auto output = std::move(outbound);
        outbound.clear();
        if (install_handshake_keys_on_drain) {
            state_value = flowq::quic::handshake_state::handshaking;
            keys.handshake = true;
        }
        if (confirm_handshake_on_drain) {
            state_value = flowq::quic::handshake_state::handshake_confirmed;
            keys.handshake = true;
            keys.application = true;
        }
        return output;
    }

    flowq::quic::handshake_state state_value{flowq::quic::handshake_state::idle};
    flowq::quic::tls_key_availability keys{};
    flowq::quic::crypto_provider_status status{flowq::quic::crypto_provider_status::unavailable()};
    bool install_handshake_keys_on_receive{};
    bool confirm_handshake_on_receive{};
    bool install_handshake_keys_on_drain{};
    bool confirm_handshake_on_drain{};
    std::vector<flowq::quic::crypto_bytes> received;
    std::vector<flowq::quic::crypto_bytes> outbound;
};

class provider_backed_packet_protector final : public flowq::quic::packet_protector {
public:
    explicit provider_backed_packet_protector(flowq::quic::protection_level level) : level_{level} {}

    flowq::quic::protection_level level() const noexcept override {
        return level_;
    }

    flowq::quic::packet_security_level security_level() const noexcept override {
        return flowq::quic::packet_security_level::authenticated_encrypted;
    }

    flowq::quic::crypto_provider_status provider_status() const noexcept override {
        return flowq::quic::crypto_provider_status::available(
            flowq::quic::cipher_suite::aes_128_gcm_sha256,
            flowq::quic::crypto_capabilities{true, true, true, true, true});
    }

    flowq::quic::packet_protection_result protect(std::span<const std::byte> plaintext) const override {
        return {flowq::buffer{plaintext}, {}};
    }

    flowq::quic::packet_protection_result unprotect(std::span<const std::byte> protected_payload) const override {
        return {flowq::buffer{protected_payload}, {}};
    }

private:
    flowq::quic::protection_level level_{};
};

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
    std::uint64_t initial_connection_send_max_data = UINT64_MAX,
    std::size_t max_packet_payload_size = SIZE_MAX,
    flowq::quic::packet_protection_policy protection_policy = flowq::quic::packet_protection_policy::test_allowed,
    bool disable_active_migration = false) {
    flowq::quic::connection_loop_config config{
        flowq::quic::connection_role::client,
        1,
        std::move(local),
        std::move(remote),
        std::move(peer),
        &protector,
        &protector,
        &protector,
        flowq::quic::packet_pipeline_config{1200},
        initial_stream_send_max_data,
        initial_connection_send_max_data,
        max_packet_payload_size,
        protection_policy
    };
    config.disable_active_migration = disable_active_migration;
    return flowq::quic::connection_loop{std::move(config)};
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

TEST_CASE("connection loop preserves queued frames that exceed payload budget for a later flush") {
    flowq::quic::plaintext_packet_protector protector{};
    flowq::endpoint peer{"127.0.0.1", 4433, "hq-interop"};
    auto loop = make_loop(cid({0x01}), cid({0x02}), peer, protector, UINT64_MAX, UINT64_MAX, 7);
    loop.queue_initial({
        flowq::quic::frame{flowq::quic::ping_frame{}},
        flowq::quic::frame{flowq::quic::stream_frame{0, 0, false, true, false, text("abc")}},
        flowq::quic::frame{flowq::quic::ping_frame{}}
    });

    loop.flush();
    auto first_datagram = require_single_outbound(loop.drain_actions());
    auto first = flowq::quic::parse_long_packet(first_datagram.payload, protector);
    REQUIRE(first.ok());
    REQUIRE(first.frames.size() == 2);
    CHECK(std::holds_alternative<flowq::quic::ping_frame>(first.frames[0]));
    CHECK(std::holds_alternative<flowq::quic::stream_frame>(first.frames[1]));

    loop.flush();
    auto second_datagram = require_single_outbound(loop.drain_actions());
    auto second = flowq::quic::parse_long_packet(second_datagram.payload, protector);
    REQUIRE(second.ok());
    REQUIRE(second.frames.size() == 1);
    CHECK(std::holds_alternative<flowq::quic::ping_frame>(second.frames[0]));
    CHECK(loop.sent_packets(flowq::quic::packet_number_space::initial).packets().size() == 2);
    CHECK(loop.sent_packets(flowq::quic::packet_number_space::initial).packets()[0].packet_number == 0);
    CHECK(loop.sent_packets(flowq::quic::packet_number_space::initial).packets()[1].packet_number == 1);
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

TEST_CASE("connection loop learns peer source connection ID from long headers") {
    flowq::quic::plaintext_packet_protector protector{};
    flowq::endpoint server_peer{"server", 4433, "hq-interop"};
    flowq::endpoint client_peer{"client", 1111, "hq-interop"};
    auto client = make_loop(cid({0x01}), cid({0x02}), server_peer, protector);
    auto server = make_loop(cid({0xa0, 0xa1}), cid({0x01}), client_peer, protector);

    server.queue_handshake({flowq::quic::frame{flowq::quic::ping_frame{}}});
    server.flush(at(0ms));
    auto inbound = require_single_outbound(server.drain_actions());

    client.on_datagram(flowq::quic::inbound_datagram{std::move(inbound.payload), server_peer}, at(1ms));
    REQUIRE(client.drain_actions().size() == 1);

    client.queue_handshake({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.flush(at(2ms));
    auto response = require_single_outbound(client.drain_actions());
    auto decoded = flowq::quic::decode_packet_header(response.payload);
    REQUIRE(decoded.ok());
    REQUIRE(std::holds_alternative<flowq::quic::handshake_header>(decoded.header));
    const auto& handshake = std::get<flowq::quic::handshake_header>(decoded.header);
    CHECK(handshake.destination_connection_id.bytes.size() == 2);
    CHECK(handshake.destination_connection_id.bytes.data()[0] == std::byte{0xa0});
    CHECK(handshake.destination_connection_id.bytes.data()[1] == std::byte{0xa1});
}

TEST_CASE("connection loop accepts coalesced long datagrams with trailing zero padding") {
    flowq::quic::plaintext_packet_protector protector{};
    flowq::endpoint client_peer{"client", 1111, "hq-interop"};
    flowq::endpoint server_peer{"server", 4433, "hq-interop"};
    auto client = make_loop(cid({0x01}), cid({0x02}), server_peer, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), client_peer, protector);

    client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.flush(at(0ms));
    auto inbound = require_single_outbound(client.drain_actions());
    std::vector<std::byte> padded{
        inbound.payload.data(),
        inbound.payload.data() + inbound.payload.size()
    };
    padded.insert(padded.end(), {std::byte{0x00}, std::byte{0x00}, std::byte{0x00}});

    server.on_datagram(flowq::quic::inbound_datagram{flowq::buffer{padded}, client_peer}, at(1ms));

    auto actions = server.drain_actions();
    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::received_packet_event>(actions[0]));
    const auto& received = std::get<flowq::quic::received_packet_event>(actions[0]);
    CHECK(received.number.space == flowq::quic::packet_number_space::initial);
    CHECK(received.number.value == 0);
}

TEST_CASE("connection loop enforces server anti-amplification limit before peer address validation") {
    flowq::quic::plaintext_packet_protector protector{};
    flowq::endpoint client_peer{"client", 1111, "hq-interop"};
    flowq::endpoint server_peer{"server", 4433, "hq-interop"};
    auto client = make_loop(cid({0x01}), cid({0x02}), server_peer, protector);

    flowq::quic::connection_loop_config server_config{};
    server_config.role = flowq::quic::connection_role::server;
    server_config.local_connection_id = cid({0x02});
    server_config.remote_connection_id = cid({0x01});
    server_config.peer = client_peer;
    server_config.initial_protector = &protector;
    server_config.handshake_protector = &protector;
    server_config.application_protector = &protector;
    server_config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    server_config.pipeline = flowq::quic::packet_pipeline_config{8192};
    auto server = flowq::quic::connection_loop{std::move(server_config)};

    client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.flush(at(0ms));
    auto first_client_datagram = require_single_outbound(client.drain_actions());
    const auto first_client_datagram_size = first_client_datagram.payload.size();

    server.on_datagram(
        flowq::quic::inbound_datagram{std::move(first_client_datagram.payload), client_peer},
        at(1ms));
    REQUIRE(server.drain_actions().size() == 1);

    const auto oversized_response_bytes = (first_client_datagram_size * 3U) + 128U;
    server.queue_initial({
        flowq::quic::frame{flowq::quic::crypto_frame{0, text(std::string(oversized_response_bytes, 's'))}}
    });
    server.flush(at(2ms));

    CHECK(server.drain_actions().empty());
    CHECK(server.sent_packets(flowq::quic::packet_number_space::initial).packets().empty());

    client.queue_initial({
        flowq::quic::frame{flowq::quic::crypto_frame{0, text(std::string(600, 'c'))}}
    });
    client.flush(at(3ms));
    auto second_client_datagram = require_single_outbound(client.drain_actions());
    server.on_datagram(
        flowq::quic::inbound_datagram{std::move(second_client_datagram.payload), client_peer},
        at(4ms));
    REQUIRE(server.drain_actions().size() == 1);

    server.flush(at(5ms));

    auto unblocked = require_single_outbound(server.drain_actions());
    CHECK_FALSE(unblocked.payload.empty());
    CHECK(server.sent_packets(flowq::quic::packet_number_space::initial).packets().size() == 1);
}

TEST_CASE("connection loop lets prevalidated server peer send before receiving packets") {
    flowq::quic::plaintext_packet_protector protector{};

    flowq::quic::connection_loop_config server_config{};
    server_config.role = flowq::quic::connection_role::server;
    server_config.local_connection_id = cid({0x02});
    server_config.remote_connection_id = cid({0x01});
    server_config.peer = flowq::endpoint{"client", 1111, "hq-interop"};
    server_config.initial_protector = &protector;
    server_config.handshake_protector = &protector;
    server_config.application_protector = &protector;
    server_config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    server_config.peer_address_validated = true;
    auto server = flowq::quic::connection_loop{std::move(server_config)};

    server.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    server.flush(at(0ms));

    auto datagram = require_single_outbound(server.drain_actions());
    CHECK_FALSE(datagram.payload.empty());
    CHECK(server.sent_packets(flowq::quic::packet_number_space::initial).packets().size() == 1);
}

TEST_CASE("connection loop ignores discarded Initial packet space") {
    flowq::quic::plaintext_packet_protector protector{};
    flowq::endpoint server_peer{"server", 4433, "hq-interop"};
    flowq::endpoint client_peer{"client", 1111, "hq-interop"};
    auto client = make_loop(cid({0x01}), cid({0x02}), server_peer, protector);

    flowq::quic::key_lifecycle_state lifecycle{};
    lifecycle.discard(flowq::quic::packet_number_space::initial);
    flowq::quic::connection_loop_config server_config{};
    server_config.role = flowq::quic::connection_role::server;
    server_config.local_connection_id = cid({0x02});
    server_config.remote_connection_id = cid({0x01});
    server_config.peer = client_peer;
    server_config.initial_protector = &protector;
    server_config.handshake_protector = &protector;
    server_config.application_protector = &protector;
    server_config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    server_config.key_lifecycle = lifecycle;
    auto server = flowq::quic::connection_loop{std::move(server_config)};

    client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.flush(at(0ms));
    auto datagram = require_single_outbound(client.drain_actions());

    server.on_datagram(flowq::quic::inbound_datagram{std::move(datagram.payload), datagram.peer});
    CHECK(server.drain_actions().empty());

    server.acknowledge(flowq::quic::packet_number_space::initial);
    CHECK(server.drain_actions().empty());
}

TEST_CASE("connection loop does not flush or recover discarded packet spaces") {
    flowq::quic::plaintext_packet_protector protector{};
    flowq::quic::key_lifecycle_state lifecycle{};
    lifecycle.discard(flowq::quic::packet_number_space::initial);

    flowq::quic::connection_loop_config config{};
    config.role = flowq::quic::connection_role::client;
    config.local_connection_id = cid({0x01});
    config.remote_connection_id = cid({0x02});
    config.peer = flowq::endpoint{"server", 4433, "hq-interop"};
    config.initial_protector = &protector;
    config.handshake_protector = &protector;
    config.application_protector = &protector;
    config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    config.key_lifecycle = lifecycle;
    auto loop = flowq::quic::connection_loop{std::move(config)};

    loop.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.flush(at(0ms));

    CHECK(loop.drain_actions().empty());
    CHECK(loop.sent_packets(flowq::quic::packet_number_space::initial).packets().empty());
    CHECK_FALSE(loop.next_recovery_timer(at(0ms)).has_value());
}

TEST_CASE("connection loop applies TLS key lifecycle discard decisions") {
    flowq::quic::plaintext_packet_protector protector{};
    recording_tls_adapter adapter{};
    adapter.state_value = flowq::quic::handshake_state::handshaking;
    adapter.keys.handshake = true;

    flowq::quic::connection_loop_config config{};
    config.role = flowq::quic::connection_role::client;
    config.local_connection_id = cid({0x01});
    config.remote_connection_id = cid({0x02});
    config.peer = flowq::endpoint{"server", 4433, "hq-interop"};
    config.initial_protector = &protector;
    config.handshake_protector = &protector;
    config.application_protector = &protector;
    config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    config.tls_adapter = &adapter;
    auto loop = flowq::quic::connection_loop{std::move(config)};

    loop.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.queue_handshake({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.flush(at(0ms));

    auto actions = loop.drain_actions();
    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::outbound_datagram>(actions[0]));
    CHECK(loop.sent_packets(flowq::quic::packet_number_space::initial).packets().empty());
    CHECK(loop.sent_packets(flowq::quic::packet_number_space::handshake).packets().size() == 1);

    recording_tls_adapter confirmed_adapter{};
    confirmed_adapter.state_value = flowq::quic::handshake_state::handshake_confirmed;
    confirmed_adapter.keys.handshake = true;
    confirmed_adapter.keys.application = true;
    flowq::quic::connection_loop_config confirmed_config{};
    confirmed_config.role = flowq::quic::connection_role::client;
    confirmed_config.local_connection_id = cid({0x01});
    confirmed_config.remote_connection_id = cid({0x02});
    confirmed_config.peer = flowq::endpoint{"server", 4433, "hq-interop"};
    confirmed_config.initial_protector = &protector;
    confirmed_config.handshake_protector = &protector;
    confirmed_config.application_protector = &protector;
    confirmed_config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    confirmed_config.tls_adapter = &confirmed_adapter;
    auto confirmed_loop = flowq::quic::connection_loop{std::move(confirmed_config)};

    confirmed_loop.queue_handshake({flowq::quic::frame{flowq::quic::ping_frame{}}});
    confirmed_loop.flush(at(0ms));

    auto confirmed_actions = confirmed_loop.drain_actions();
    REQUIRE(confirmed_actions.size() == 1);
    CHECK(std::holds_alternative<flowq::quic::outbound_datagram>(confirmed_actions[0]));
    CHECK(confirmed_loop.sent_packets(flowq::quic::packet_number_space::handshake).packets().size() == 1);
}

TEST_CASE("connection loop refreshes key lifecycle after inbound TLS CRYPTO changes") {
    flowq::quic::plaintext_packet_protector protector{};
    recording_tls_adapter adapter{};
    adapter.install_handshake_keys_on_receive = true;
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);

    flowq::quic::connection_loop_config server_config{};
    server_config.role = flowq::quic::connection_role::server;
    server_config.local_connection_id = cid({0x02});
    server_config.remote_connection_id = cid({0x01});
    server_config.peer = flowq::endpoint{"client", 1111, "hq-interop"};
    server_config.initial_protector = &protector;
    server_config.handshake_protector = &protector;
    server_config.application_protector = &protector;
    server_config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    server_config.tls_adapter = &adapter;
    server_config.peer_address_validated = true;
    auto server = flowq::quic::connection_loop{std::move(server_config)};

    server.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    server.flush(at(0ms));
    auto server_initial = require_single_outbound(server.drain_actions());
    REQUIRE(server.next_recovery_timer(at(1ms)).has_value());

    client.queue_initial({flowq::quic::frame{flowq::quic::crypto_frame{0, text("client hello")}}});
    client.flush(at(2ms));
    auto client_crypto = require_single_outbound(client.drain_actions());

    server.on_datagram(flowq::quic::inbound_datagram{std::move(client_crypto.payload), client_crypto.peer});

    auto received_actions = server.drain_actions();
    REQUIRE(received_actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::received_packet_event>(received_actions[0]));
    CHECK_FALSE(server.next_recovery_timer(at(3ms)).has_value());
    CHECK(server.sent_packets(flowq::quic::packet_number_space::initial).packets().empty());
    server.acknowledge(flowq::quic::packet_number_space::initial);
    CHECK(server.drain_actions().empty());
    (void)server_initial;
}

TEST_CASE("connection loop refreshes key lifecycle after draining TLS CRYPTO") {
    flowq::quic::plaintext_packet_protector protector{};
    recording_tls_adapter adapter{};
    adapter.state_value = flowq::quic::handshake_state::handshaking;
    adapter.keys.handshake = true;
    adapter.outbound.push_back(flowq::quic::crypto_bytes{flowq::quic::tls_encryption_level::handshake, 0, text("final flight")});
    adapter.confirm_handshake_on_drain = true;

    flowq::quic::connection_loop_config config{};
    config.role = flowq::quic::connection_role::server;
    config.local_connection_id = cid({0x02});
    config.remote_connection_id = cid({0x01});
    config.peer = flowq::endpoint{"client", 1111, "hq-interop"};
    config.initial_protector = &protector;
    config.handshake_protector = &protector;
    config.application_protector = &protector;
    config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    config.tls_adapter = &adapter;
    auto loop = flowq::quic::connection_loop{std::move(config)};

    loop.queue_handshake({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.flush(at(0ms));

    auto actions = loop.drain_actions();
    REQUIRE(actions.size() == 1);
    CHECK(std::holds_alternative<flowq::quic::outbound_datagram>(actions[0]));
    CHECK(loop.sent_packets(flowq::quic::packet_number_space::handshake).packets().size() == 1);
    CHECK(loop.next_recovery_timer(at(1ms)).has_value());
}

TEST_CASE("connection loop discards Initial space after draining TLS handshake keys") {
    flowq::quic::plaintext_packet_protector protector{};
    recording_tls_adapter adapter{};
    adapter.outbound.push_back(flowq::quic::crypto_bytes{flowq::quic::tls_encryption_level::initial, 0, text("late initial flight")});
    adapter.install_handshake_keys_on_drain = true;

    flowq::quic::connection_loop_config config{};
    config.role = flowq::quic::connection_role::server;
    config.local_connection_id = cid({0x02});
    config.remote_connection_id = cid({0x01});
    config.peer = flowq::endpoint{"client", 1111, "hq-interop"};
    config.initial_protector = &protector;
    config.handshake_protector = &protector;
    config.application_protector = &protector;
    config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    config.tls_adapter = &adapter;
    auto loop = flowq::quic::connection_loop{std::move(config)};

    loop.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.flush(at(0ms));

    CHECK(loop.drain_actions().empty());
    CHECK(loop.sent_packets(flowq::quic::packet_number_space::initial).packets().empty());
    CHECK_FALSE(loop.next_recovery_timer(at(1ms)).has_value());
}

TEST_CASE("connection loop feeds inbound CRYPTO frames to TLS adapter by packet space") {
    flowq::quic::plaintext_packet_protector protector{};
    recording_tls_adapter adapter{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    flowq::quic::connection_loop_config server_config{};
    server_config.role = flowq::quic::connection_role::server;
    server_config.local_connection_id = cid({0x02});
    server_config.remote_connection_id = cid({0x01});
    server_config.peer = flowq::endpoint{"client", 1111, "hq-interop"};
    server_config.initial_protector = &protector;
    server_config.handshake_protector = &protector;
    server_config.application_protector = &protector;
    server_config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    server_config.tls_adapter = &adapter;
    auto server = flowq::quic::connection_loop{std::move(server_config)};

    client.queue_initial({flowq::quic::frame{flowq::quic::crypto_frame{9, text("client hello")}}});
    client.flush();
    auto datagram = require_single_outbound(client.drain_actions());

    server.on_datagram(flowq::quic::inbound_datagram{std::move(datagram.payload), datagram.peer});

    REQUIRE(adapter.received.size() == 1);
    CHECK(adapter.received[0].level == flowq::quic::tls_encryption_level::initial);
    CHECK(adapter.received[0].offset == 9);
    CHECK(as_string(adapter.received[0].data) == "client hello");
}

TEST_CASE("connection loop pumps TLS adapter CRYPTO bytes into packet-space frames") {
    flowq::quic::plaintext_packet_protector protector{};
    recording_tls_adapter adapter{};
    adapter.outbound.push_back(flowq::quic::crypto_bytes{flowq::quic::tls_encryption_level::handshake, 5, text("server flight")});
    flowq::quic::connection_loop_config config{};
    config.role = flowq::quic::connection_role::server;
    config.local_connection_id = cid({0x02});
    config.remote_connection_id = cid({0x01});
    config.peer = flowq::endpoint{"client", 1111, "hq-interop"};
    config.initial_protector = &protector;
    config.handshake_protector = &protector;
    config.application_protector = &protector;
    config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    config.tls_adapter = &adapter;
    config.peer_address_validated = true;
    auto loop = flowq::quic::connection_loop{std::move(config)};

    loop.flush();

    auto datagram = require_single_outbound(loop.drain_actions());
    auto parsed = flowq::quic::parse_long_packet(datagram.payload, protector);
    REQUIRE(parsed.ok());
    CHECK(parsed.number.space == flowq::quic::packet_number_space::handshake);
    REQUIRE(parsed.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::crypto_frame>(parsed.frames[0]));
    const auto& crypto = std::get<flowq::quic::crypto_frame>(parsed.frames[0]);
    CHECK(crypto.offset == 5);
    CHECK(as_string(crypto.data) == "server flight");
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

TEST_CASE("connection loop exposes recovery timer for outstanding ack eliciting packets") {
    flowq::quic::plaintext_packet_protector protector{};
    auto loop = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    loop.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});

    loop.flush(at(0ms));
    auto timer = loop.next_recovery_timer(at(100ms));

    REQUIRE(timer.has_value());
    CHECK(timer->space == flowq::quic::packet_number_space::initial);
    CHECK(timer->mode == flowq::quic::loss_timer_mode::pto);
    CHECK(timer->deadline == at(999ms));
}

TEST_CASE("connection loop clears recovery timer after packet acknowledgment") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);
    client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.flush(at(0ms));
    auto outbound = require_single_outbound(client.drain_actions());
    REQUIRE(client.next_recovery_timer(at(1ms)).has_value());

    server.on_datagram(flowq::quic::inbound_datagram{std::move(outbound.payload), outbound.peer});
    (void)server.drain_actions();
    server.acknowledge(flowq::quic::packet_number_space::initial);
    auto ack_datagram = require_single_outbound(server.drain_actions());
    client.on_datagram(flowq::quic::inbound_datagram{std::move(ack_datagram.payload), ack_datagram.peer});

    CHECK_FALSE(client.next_recovery_timer(at(2ms)).has_value());
}

TEST_CASE("connection loop recovery timer polling does not move PTO deadline") {
    flowq::quic::plaintext_packet_protector protector{};
    auto loop = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    loop.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.flush(at(0ms));

    auto first = loop.next_recovery_timer(at(100ms));
    auto later = loop.next_recovery_timer(at(500ms));

    REQUIRE(first.has_value());
    REQUIRE(later.has_value());
    CHECK(later->deadline == first->deadline);
}

TEST_CASE("connection loop ACK only packets do not arm recovery timers") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);
    client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.flush(at(0ms));
    auto outbound = require_single_outbound(client.drain_actions());
    server.on_datagram(flowq::quic::inbound_datagram{std::move(outbound.payload), outbound.peer});
    (void)server.drain_actions();

    server.acknowledge(flowq::quic::packet_number_space::initial);

    CHECK_FALSE(server.next_recovery_timer(at(1ms)).has_value());
}

TEST_CASE("connection loop recovery timer expiry reports time threshold losses") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);
    client.update_rtt(flowq::quic::rtt_sample{80ms, 0ms, 25ms, true});
    client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.flush(at(0ms));
    (void)client.drain_actions();
    client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.flush(at(10ms));
    (void)client.drain_actions();

    server.queue_initial({flowq::quic::frame{flowq::quic::ack_frame{1, 0, 0, {}}}});
    server.flush(at(20ms));
    auto ack_datagram = require_single_outbound(server.drain_actions());
    client.on_datagram(flowq::quic::inbound_datagram{std::move(ack_datagram.payload), ack_datagram.peer});
    auto result = client.on_recovery_timer(flowq::quic::packet_number_space::initial, at(100ms));

    CHECK(result.space == flowq::quic::packet_number_space::initial);
    CHECK(result.newly_lost == std::vector<std::uint64_t>{0});
    const auto& packets = client.sent_packets(flowq::quic::packet_number_space::initial).packets();
    REQUIRE(packets.size() == 2);
    CHECK(packets[0].state == flowq::quic::sent_packet_state::lost);
    CHECK(packets[1].state == flowq::quic::sent_packet_state::acknowledged);
}

TEST_CASE("connection loop does not arm loss timer without largest acknowledged packet") {
    flowq::quic::plaintext_packet_protector protector{};
    auto loop = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    loop.update_rtt(flowq::quic::rtt_sample{80ms, 0ms, 25ms, true});
    loop.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.flush(at(0ms));

    auto timer = loop.next_recovery_timer(at(100ms));

    REQUIRE(timer.has_value());
    CHECK(timer->space == flowq::quic::packet_number_space::initial);
    CHECK(timer->mode == flowq::quic::loss_timer_mode::pto);
    CHECK(timer->deadline == at(240ms));
}

TEST_CASE("connection loop does not arm loss timer for packets above largest acknowledged") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);
    client.update_rtt(flowq::quic::rtt_sample{80ms, 0ms, 25ms, true});
    client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.flush(at(0ms));
    (void)client.drain_actions();
    client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.flush(at(10ms));
    (void)client.drain_actions();

    server.queue_initial({flowq::quic::frame{flowq::quic::ack_frame{0, 0, 0, {}}}});
    server.flush(at(20ms));
    auto ack_datagram = require_single_outbound(server.drain_actions());
    client.on_datagram(flowq::quic::inbound_datagram{std::move(ack_datagram.payload), ack_datagram.peer});
    auto timer = client.next_recovery_timer(at(100ms));
    auto expired = client.on_recovery_timer(flowq::quic::packet_number_space::initial, at(100ms));

    REQUIRE(timer.has_value());
    CHECK(timer->mode == flowq::quic::loss_timer_mode::pto);
    CHECK(timer->deadline == at(250ms));
    CHECK(expired.newly_lost.empty());
    REQUIRE(expired.next_deadline.has_value());
    CHECK(*expired.next_deadline == at(250ms));
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

TEST_CASE("connection loop records STREAM ranges carried by sent packets") {
    flowq::quic::plaintext_packet_protector protector{};
    auto loop = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    const std::vector<std::uint64_t> order{0};
    REQUIRE(loop.append_stream_data(0, text("hello")).ok());
    auto scheduled = loop.schedule_stream_frames(order, 1, 16);
    REQUIRE(scheduled.ok());
    loop.queue_initial(std::move(scheduled.frames));

    loop.flush(at(0ms));

    auto ranges = loop.sent_stream_ranges(flowq::quic::packet_number_space::initial, 0);
    REQUIRE(ranges.size() == 1);
    CHECK(ranges[0].stream_id == 0);
    CHECK(ranges[0].range.offset == 0);
    CHECK(ranges[0].range.length == 5);
    CHECK_FALSE(ranges[0].range.fin);
}

TEST_CASE("connection loop records multiple STREAM ranges carried by one packet") {
    flowq::quic::plaintext_packet_protector protector{};
    auto loop = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    const std::vector<std::uint64_t> order{0, 4};
    REQUIRE(loop.append_stream_data(0, text("alpha")).ok());
    REQUIRE(loop.append_stream_data(4, text("beta")).ok());
    auto scheduled = loop.schedule_stream_frames(order, 2, 16);
    REQUIRE(scheduled.ok());
    loop.queue_initial(std::move(scheduled.frames));

    loop.flush(at(0ms));

    auto ranges = loop.sent_stream_ranges(flowq::quic::packet_number_space::initial, 0);
    REQUIRE(ranges.size() == 2);
    CHECK(ranges[0].stream_id == 0);
    CHECK(ranges[0].range.offset == 0);
    CHECK(ranges[0].range.length == 5);
    CHECK(ranges[1].stream_id == 4);
    CHECK(ranges[1].range.offset == 0);
    CHECK(ranges[1].range.length == 4);
}

TEST_CASE("connection loop records only selected STREAM ranges when packet budget splits frames") {
    flowq::quic::plaintext_packet_protector protector{};
    auto loop = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector, UINT64_MAX, UINT64_MAX, 6);
    loop.queue_initial({
        flowq::quic::frame{flowq::quic::stream_frame{0, 0, false, true, false, text("abc")}},
        flowq::quic::frame{flowq::quic::stream_frame{4, 0, false, true, false, text("def")}}
    });

    loop.flush(at(0ms));
    auto first_ranges = loop.sent_stream_ranges(flowq::quic::packet_number_space::initial, 0);
    auto second_ranges_before_flush = loop.sent_stream_ranges(flowq::quic::packet_number_space::initial, 1);
    loop.flush(at(1ms));
    auto second_ranges = loop.sent_stream_ranges(flowq::quic::packet_number_space::initial, 1);

    REQUIRE(first_ranges.size() == 1);
    CHECK(first_ranges[0].stream_id == 0);
    CHECK(second_ranges_before_flush.empty());
    REQUIRE(second_ranges.size() == 1);
    CHECK(second_ranges[0].stream_id == 4);
}

TEST_CASE("connection loop keeps sent STREAM range ledger out of application space") {
    flowq::quic::plaintext_packet_protector protector{};
    auto loop = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    const std::vector<std::uint64_t> order{0};
    REQUIRE(loop.append_stream_data(0, text("hello")).ok());
    auto scheduled = loop.schedule_stream_frames(order, 1, 16);
    REQUIRE(scheduled.ok());
    loop.queue_initial(std::move(scheduled.frames));
    loop.flush(at(0ms));

    CHECK(loop.sent_stream_ranges(flowq::quic::packet_number_space::application, 0).empty());
}

TEST_CASE("connection loop maps packet loss to stream retransmission state") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);
    const std::vector<std::uint64_t> order{0};
    REQUIRE(client.append_stream_data(0, text("hello")).ok());
    auto scheduled = client.schedule_stream_frames(order, 1, 16);
    REQUIRE(scheduled.ok());
    client.queue_initial(std::move(scheduled.frames));
    client.flush(at(0ms));
    (void)client.drain_actions();

    for (int index = 0; index < 4; ++index) {
        client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
        client.flush(at(std::chrono::milliseconds{10 + index}));
        (void)client.drain_actions();
    }

    server.queue_initial({flowq::quic::frame{flowq::quic::ack_frame{4, 0, 0, {}}}});
    server.flush(at(20ms));
    auto ack_datagram = require_single_outbound(server.drain_actions());
    client.on_datagram(flowq::quic::inbound_datagram{std::move(ack_datagram.payload), ack_datagram.peer});
    auto retransmit = client.schedule_stream_frames(order, 1, 16);

    REQUIRE(retransmit.ok());
    REQUIRE(retransmit.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(retransmit.frames[0]));
    const auto& stream = std::get<flowq::quic::stream_frame>(retransmit.frames[0]);
    CHECK(stream.stream_id == 0);
    CHECK(stream.offset == 0);
    CHECK(as_string(stream.data) == "hello");
}

TEST_CASE("connection loop retransmits lost STREAM data without fresh connection credit") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector, UINT64_MAX, 5);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);
    const std::vector<std::uint64_t> order{0};
    REQUIRE(client.append_stream_data(0, text("hello")).ok());
    auto scheduled = client.schedule_stream_frames(order, 1, 16);
    REQUIRE(scheduled.ok());
    REQUIRE(scheduled.frames.size() == 1);
    client.queue_initial(std::move(scheduled.frames));
    client.flush(at(0ms));
    (void)client.drain_actions();

    for (int index = 0; index < 4; ++index) {
        client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
        client.flush(at(std::chrono::milliseconds{10 + index}));
        (void)client.drain_actions();
    }

    server.queue_initial({flowq::quic::frame{flowq::quic::ack_frame{4, 0, 0, {}}}});
    server.flush(at(20ms));
    auto ack_datagram = require_single_outbound(server.drain_actions());
    client.on_datagram(flowq::quic::inbound_datagram{std::move(ack_datagram.payload), ack_datagram.peer});
    auto retransmit = client.schedule_stream_frames(order, 1, 16);

    REQUIRE(retransmit.ok());
    REQUIRE(retransmit.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(retransmit.frames[0]));
    const auto& stream = std::get<flowq::quic::stream_frame>(retransmit.frames[0]);
    CHECK(stream.offset == 0);
    CHECK(as_string(stream.data) == "hello");
}

TEST_CASE("connection loop maps recovery timer loss to stream retransmission state") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);
    const std::vector<std::uint64_t> order{0};
    client.update_rtt(flowq::quic::rtt_sample{80ms, 0ms, 25ms, true});
    REQUIRE(client.append_stream_data(0, text("hello")).ok());
    auto scheduled = client.schedule_stream_frames(order, 1, 16);
    REQUIRE(scheduled.ok());
    client.queue_initial(std::move(scheduled.frames));
    client.flush(at(0ms));
    (void)client.drain_actions();
    client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.flush(at(10ms));
    (void)client.drain_actions();

    server.queue_initial({flowq::quic::frame{flowq::quic::ack_frame{1, 0, 0, {}}}});
    server.flush(at(20ms));
    auto ack_datagram = require_single_outbound(server.drain_actions());
    client.on_datagram(flowq::quic::inbound_datagram{std::move(ack_datagram.payload), ack_datagram.peer});
    auto lost = client.on_recovery_timer(flowq::quic::packet_number_space::initial, at(100ms));
    auto retransmit = client.schedule_stream_frames(order, 1, 16);

    CHECK(lost.newly_lost == std::vector<std::uint64_t>{0});
    REQUIRE(retransmit.ok());
    REQUIRE(retransmit.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(retransmit.frames[0]));
    const auto& stream = std::get<flowq::quic::stream_frame>(retransmit.frames[0]);
    CHECK(stream.stream_id == 0);
    CHECK(stream.offset == 0);
    CHECK(as_string(stream.data) == "hello");
}

TEST_CASE("connection loop maps packet ACK to suppress later stream retransmission") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);
    const std::vector<std::uint64_t> order{0};
    REQUIRE(client.append_stream_data(0, text("hello")).ok());
    auto scheduled = client.schedule_stream_frames(order, 1, 16);
    REQUIRE(scheduled.ok());
    client.queue_initial(std::move(scheduled.frames));
    client.flush(at(0ms));
    (void)client.drain_actions();

    server.queue_initial({flowq::quic::frame{flowq::quic::ack_frame{0, 0, 0, {}}}});
    server.flush(at(10ms));
    auto ack_datagram = require_single_outbound(server.drain_actions());
    client.on_datagram(flowq::quic::inbound_datagram{std::move(ack_datagram.payload), ack_datagram.peer});
    auto lost = client.on_recovery_timer(flowq::quic::packet_number_space::initial, at(1000ms));
    auto retransmit = client.schedule_stream_frames(order, 1, 16);

    CHECK(lost.newly_lost.empty());
    REQUIRE(retransmit.ok());
    CHECK(retransmit.frames.empty());
}

TEST_CASE("connection loop ignores lost manual STREAM frames outside send state") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);
    const std::vector<std::uint64_t> order{0};
    client.queue_initial({flowq::quic::frame{flowq::quic::stream_frame{0, 0, false, true, false, text("external")}}});
    client.flush(at(0ms));
    (void)client.drain_actions();
    for (int index = 0; index < 4; ++index) {
        client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
        client.flush(at(std::chrono::milliseconds{10 + index}));
        (void)client.drain_actions();
    }

    server.queue_initial({flowq::quic::frame{flowq::quic::ack_frame{4, 0, 0, {}}}});
    server.flush(at(20ms));
    auto ack_datagram = require_single_outbound(server.drain_actions());
    client.on_datagram(flowq::quic::inbound_datagram{std::move(ack_datagram.payload), ack_datagram.peer});
    auto retransmit = client.schedule_stream_frames(order, 1, 16);

    REQUIRE(retransmit.ok());
    CHECK(retransmit.frames.empty());
}

TEST_CASE("connection loop ignores lost manual STREAM frames for existing unsent send state") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector, UINT64_MAX, 0);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);
    const std::vector<std::uint64_t> order{0};
    REQUIRE(client.append_stream_data(0, text("external")).ok());
    client.queue_initial({flowq::quic::frame{flowq::quic::stream_frame{0, 0, false, true, false, text("external")}}});
    client.flush(at(0ms));
    (void)client.drain_actions();
    for (int index = 0; index < 4; ++index) {
        client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
        client.flush(at(std::chrono::milliseconds{10 + index}));
        (void)client.drain_actions();
    }

    server.queue_initial({flowq::quic::frame{flowq::quic::ack_frame{4, 0, 0, {}}}});
    server.flush(at(20ms));
    auto ack_datagram = require_single_outbound(server.drain_actions());
    client.on_datagram(flowq::quic::inbound_datagram{std::move(ack_datagram.payload), ack_datagram.peer});
    auto scheduled = client.schedule_stream_frames(order, 1, 16);

    REQUIRE(scheduled.ok());
    REQUIRE(scheduled.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::data_blocked_frame>(scheduled.frames[0]));
}

TEST_CASE("connection loop ignores acknowledged manual STREAM frames for existing unsent send state") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);
    const std::vector<std::uint64_t> order{0};
    REQUIRE(client.append_stream_data(0, text("external")).ok());
    client.queue_initial({flowq::quic::frame{flowq::quic::stream_frame{0, 0, false, true, false, text("external")}}});
    client.flush(at(0ms));
    (void)client.drain_actions();

    server.queue_initial({flowq::quic::frame{flowq::quic::ack_frame{0, 0, 0, {}}}});
    server.flush(at(10ms));
    auto ack_datagram = require_single_outbound(server.drain_actions());
    client.on_datagram(flowq::quic::inbound_datagram{std::move(ack_datagram.payload), ack_datagram.peer});
    auto scheduled = client.schedule_stream_frames(order, 1, 16);

    REQUIRE(scheduled.ok());
    REQUIRE(scheduled.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(scheduled.frames[0]));
    const auto& stream = std::get<flowq::quic::stream_frame>(scheduled.frames[0]);
    CHECK(stream.offset == 0);
    CHECK(as_string(stream.data) == "external");
}

TEST_CASE("connection loop keeps manual STREAM ACK from suppressing later real retransmission") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);
    const std::vector<std::uint64_t> order{0};
    REQUIRE(client.append_stream_data(0, text("external")).ok());
    client.queue_initial({flowq::quic::frame{flowq::quic::stream_frame{0, 0, false, true, false, text("external")}}});
    client.flush(at(0ms));
    (void)client.drain_actions();
    server.queue_initial({flowq::quic::frame{flowq::quic::ack_frame{0, 0, 0, {}}}});
    server.flush(at(10ms));
    auto manual_ack = require_single_outbound(server.drain_actions());
    client.on_datagram(flowq::quic::inbound_datagram{std::move(manual_ack.payload), manual_ack.peer});

    auto scheduled = client.schedule_stream_frames(order, 1, 16);
    REQUIRE(scheduled.ok());
    client.queue_initial(std::move(scheduled.frames));
    client.flush(at(20ms));
    (void)client.drain_actions();
    for (int index = 0; index < 4; ++index) {
        client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
        client.flush(at(std::chrono::milliseconds{30 + index}));
        (void)client.drain_actions();
    }

    server.queue_initial({flowq::quic::frame{flowq::quic::ack_frame{5, 0, 0, {}}}});
    server.flush(at(40ms));
    auto loss_ack = require_single_outbound(server.drain_actions());
    client.on_datagram(flowq::quic::inbound_datagram{std::move(loss_ack.payload), loss_ack.peer});
    auto retransmit = client.schedule_stream_frames(order, 1, 16);

    REQUIRE(retransmit.ok());
    REQUIRE(retransmit.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(retransmit.frames[0]));
    const auto& retransmitted = std::get<flowq::quic::stream_frame>(retransmit.frames[0]);
    CHECK(retransmitted.offset == 0);
    CHECK(as_string(retransmitted.data) == "external");
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

TEST_CASE("connection loop keeps Initial Handshake and Application packet numbers independent") {
    flowq::quic::plaintext_packet_protector protector{};
    auto loop = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);

    loop.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.queue_handshake({flowq::quic::frame{flowq::quic::crypto_frame{0, flowq::buffer{bytes({0xaa})}}}});
    loop.queue_application({flowq::quic::frame{flowq::quic::stream_frame{0, 0, false, true, false, text("app")}}});
    loop.flush(at(0ms));

    auto actions = loop.drain_actions();
    REQUIRE(actions.size() == 3);
    CHECK(loop.sent_packets(flowq::quic::packet_number_space::initial).packets()[0].packet_number == 0);
    CHECK(loop.sent_packets(flowq::quic::packet_number_space::handshake).packets()[0].packet_number == 0);
    CHECK(loop.sent_packets(flowq::quic::packet_number_space::application).packets()[0].packet_number == 0);
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

TEST_CASE("connection loop flushes queued Application frames as a structural outbound datagram") {
    flowq::quic::plaintext_packet_protector protector{};
    auto loop = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);

    loop.queue_application({flowq::quic::frame{flowq::quic::stream_frame{0, 0, false, true, false, text("app")}}});
    loop.flush(at(0ms));

    auto datagram = require_single_outbound(loop.drain_actions());
    auto parsed = flowq::quic::parse_application_packet(datagram.payload, protector);
    REQUIRE(parsed.ok());
    CHECK(parsed.space == flowq::quic::packet_number_space::application);
    CHECK(parsed.number.value == 0);
    REQUIRE(parsed.frames.size() == 1);
    CHECK(std::holds_alternative<flowq::quic::stream_frame>(parsed.frames[0]));
    CHECK(loop.sent_packets(flowq::quic::packet_number_space::application).packets()[0].packet_number == 0);
}

TEST_CASE("connection loop parses and acknowledges structural Application packets") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);

    client.queue_application({flowq::quic::frame{flowq::quic::stream_frame{0, 0, false, true, false, text("hello")}}});
    client.flush(at(0ms));
    auto datagram = require_single_outbound(client.drain_actions());

    server.on_datagram(flowq::quic::inbound_datagram{std::move(datagram.payload), datagram.peer});
    auto received_actions = server.drain_actions();
    REQUIRE(received_actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::received_packet_event>(received_actions[0]));
    const auto& received = std::get<flowq::quic::received_packet_event>(received_actions[0]);
    CHECK(received.number.space == flowq::quic::packet_number_space::application);
    CHECK(received.number.value == 0);
    REQUIRE(received.stream_deliveries.size() == 1);
    CHECK(as_string(received.stream_deliveries[0].result.data) == "hello");

    server.acknowledge(flowq::quic::packet_number_space::application);
    auto ack_datagram = require_single_outbound(server.drain_actions());
    auto parsed_ack = flowq::quic::parse_application_packet(ack_datagram.payload, protector);
    REQUIRE(parsed_ack.ok());
    CHECK(parsed_ack.space == flowq::quic::packet_number_space::application);
    REQUIRE(parsed_ack.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::ack_frame>(parsed_ack.frames[0]));
    CHECK(std::get<flowq::quic::ack_frame>(parsed_ack.frames[0]).largest_acknowledged == 0);
}

TEST_CASE("connection loop answers Application PATH_CHALLENGE with matching PATH_RESPONSE") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);
    const std::array<std::byte, 8> challenge_data{
        std::byte{0xa0}, std::byte{0xa1}, std::byte{0xa2}, std::byte{0xa3},
        std::byte{0xa4}, std::byte{0xa5}, std::byte{0xa6}, std::byte{0xa7}
    };

    client.queue_application({flowq::quic::frame{flowq::quic::path_challenge_frame{challenge_data}}});
    client.flush(at(0ms));
    auto challenge_datagram = require_single_outbound(client.drain_actions());

    server.on_datagram(flowq::quic::inbound_datagram{std::move(challenge_datagram.payload), challenge_datagram.peer});
    auto received_actions = server.drain_actions();
    REQUIRE(received_actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::received_packet_event>(received_actions[0]));
    const auto& received = std::get<flowq::quic::received_packet_event>(received_actions[0]);
    REQUIRE(received.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::path_challenge_frame>(received.frames[0]));
    CHECK(std::get<flowq::quic::path_challenge_frame>(received.frames[0]).data == challenge_data);

    server.flush(at(1ms));
    auto response_datagram = require_single_outbound(server.drain_actions());
    auto parsed_response = flowq::quic::parse_application_packet(response_datagram.payload, protector);
    REQUIRE(parsed_response.ok());
    REQUIRE(parsed_response.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::path_response_frame>(parsed_response.frames[0]));
    CHECK(std::get<flowq::quic::path_response_frame>(parsed_response.frames[0]).data == challenge_data);
}

TEST_CASE("connection loop closes on PATH validation frames outside Application packets") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);
    const std::array<std::byte, 8> challenge_data{
        std::byte{0xb0}, std::byte{0xb1}, std::byte{0xb2}, std::byte{0xb3},
        std::byte{0xb4}, std::byte{0xb5}, std::byte{0xb6}, std::byte{0xb7}
    };

    client.queue_initial({flowq::quic::frame{flowq::quic::path_challenge_frame{challenge_data}}});
    client.flush(at(0ms));
    auto challenge_datagram = require_single_outbound(client.drain_actions());

    server.on_datagram(flowq::quic::inbound_datagram{std::move(challenge_datagram.payload), challenge_datagram.peer});
    auto actions = server.drain_actions();
    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::close_action>(actions[0]));
    CHECK(std::get<flowq::quic::close_action>(actions[0]).error.code() == flowq::error_code::protocol_error);
    CHECK(server.state() == flowq::quic::connection_loop_state::closing);

    auto response_client = make_loop(cid({0x03}), cid({0x04}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto response_server = make_loop(cid({0x04}), cid({0x03}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);
    response_client.queue_handshake({flowq::quic::frame{flowq::quic::path_response_frame{challenge_data}}});
    response_client.flush(at(0ms));
    auto response_datagram = require_single_outbound(response_client.drain_actions());

    response_server.on_datagram(flowq::quic::inbound_datagram{std::move(response_datagram.payload), response_datagram.peer});
    auto response_actions = response_server.drain_actions();
    REQUIRE(response_actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::close_action>(response_actions[0]));
    CHECK(std::get<flowq::quic::close_action>(response_actions[0]).error.code() == flowq::error_code::protocol_error);
    CHECK(response_server.state() == flowq::quic::connection_loop_state::closing);
}

TEST_CASE("connection loop scopes Application ACKs to Application sent packets") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);
    client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.queue_application({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.flush(at(0ms));
    auto outbound = client.drain_actions();
    REQUIRE(outbound.size() == 2);
    REQUIRE(std::holds_alternative<flowq::quic::outbound_datagram>(outbound[1]));

    auto app_datagram = std::get<flowq::quic::outbound_datagram>(std::move(outbound[1]));
    server.on_datagram(flowq::quic::inbound_datagram{std::move(app_datagram.payload), app_datagram.peer});
    (void)server.drain_actions();
    server.acknowledge(flowq::quic::packet_number_space::application);
    auto ack_datagram = require_single_outbound(server.drain_actions());
    client.on_datagram(flowq::quic::inbound_datagram{std::move(ack_datagram.payload), ack_datagram.peer});

    const auto& initial_packets = client.sent_packets(flowq::quic::packet_number_space::initial).packets();
    const auto& application_packets = client.sent_packets(flowq::quic::packet_number_space::application).packets();
    REQUIRE(initial_packets.size() == 1);
    REQUIRE(application_packets.size() == 1);
    CHECK(initial_packets[0].state == flowq::quic::sent_packet_state::outstanding);
    CHECK(application_packets[0].state == flowq::quic::sent_packet_state::acknowledged);
}

TEST_CASE("connection loop routes inbound RESET_STREAM to receive stream state") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);

    client.queue_application({flowq::quic::frame{flowq::quic::reset_stream_frame{0, 0x2a, 5}}});
    client.flush(at(0ms));
    auto datagram = require_single_outbound(client.drain_actions());

    server.on_datagram(flowq::quic::inbound_datagram{std::move(datagram.payload), datagram.peer});
    auto actions = server.drain_actions();

    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::received_packet_event>(actions[0]));
    const auto& event = std::get<flowq::quic::received_packet_event>(actions[0]);
    REQUIRE(event.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::reset_stream_frame>(event.frames[0]));
    const auto* stream = server.receive_stream(0);
    REQUIRE(stream != nullptr);
    CHECK(stream->reset_received());
    CHECK(stream->reset_error_code() == 0x2a);
    CHECK(stream->reset_final_size() == 5);
}

TEST_CASE("connection loop routes inbound STOP_SENDING to send stream state") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);
    const std::vector<std::uint64_t> order{0};
    REQUIRE(server.append_stream_data(0, text("hello")).ok());

    client.queue_application({flowq::quic::frame{flowq::quic::stop_sending_frame{0, 0x2a}}});
    client.flush(at(0ms));
    auto datagram = require_single_outbound(client.drain_actions());

    server.on_datagram(flowq::quic::inbound_datagram{std::move(datagram.payload), datagram.peer});
    auto actions = server.drain_actions();
    auto scheduled = server.schedule_stream_frames(order, 1, 16);

    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::received_packet_event>(actions[0]));
    const auto* stream = server.send_stream(0);
    REQUIRE(stream != nullptr);
    CHECK(stream->stop_requested());
    CHECK(stream->stop_error_code() == 0x2a);
    REQUIRE(scheduled.ok());
    CHECK(scheduled.frames.empty());
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

TEST_CASE("connection loop suppresses outbound work after a local close") {
    flowq::quic::plaintext_packet_protector protector{};
    auto loop = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);

    loop.on_datagram(flowq::quic::inbound_datagram{flowq::buffer{bytes({0xc0})}, flowq::endpoint{"peer", 9999, ""}});

    auto close_actions = loop.drain_actions();
    REQUIRE(close_actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::close_action>(close_actions[0]));
    CHECK(loop.state() == flowq::quic::connection_loop_state::closing);

    loop.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.flush(at(1ms));

    CHECK(loop.drain_actions().empty());
    CHECK_FALSE(loop.next_recovery_timer(at(2ms)).has_value());
}

TEST_CASE("connection loop ignores inbound packets after a local close") {
    flowq::quic::plaintext_packet_protector protector{};
    auto closed = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto peer = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);

    peer.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    peer.flush(at(0ms));
    auto datagram = require_single_outbound(peer.drain_actions());

    closed.on_datagram(flowq::quic::inbound_datagram{flowq::buffer{bytes({0xc0})}, flowq::endpoint{"peer", 9999, ""}});
    (void)closed.drain_actions();

    closed.on_datagram(flowq::quic::inbound_datagram{std::move(datagram.payload), datagram.peer});

    CHECK(closed.drain_actions().empty());
    CHECK(closed.state() == flowq::quic::connection_loop_state::closing);
}

TEST_CASE("connection loop enters draining after peer connection close and suppresses sends") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);

    client.queue_initial({flowq::quic::frame{flowq::quic::connection_close_frame{0x1d, 0, "peer closed"}}});
    client.flush(at(0ms));
    auto datagram = require_single_outbound(client.drain_actions());

    server.on_datagram(flowq::quic::inbound_datagram{std::move(datagram.payload), datagram.peer});

    auto close_actions = server.drain_actions();
    REQUIRE(close_actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::close_action>(close_actions[0]));
    const auto& error = std::get<flowq::quic::close_action>(close_actions[0]).error;
    CHECK(error.code() == flowq::error_code::connection_closed);
    CHECK(error.message() == "peer closed");
    CHECK(server.state() == flowq::quic::connection_loop_state::draining);

    server.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    server.flush(at(1ms));

    CHECK(server.drain_actions().empty());
}

TEST_CASE("connection loop closes on peer address change when active migration is disabled") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(
        cid({0x02}),
        cid({0x01}),
        flowq::endpoint{"client", 1111, "hq-interop"},
        protector,
        UINT64_MAX,
        UINT64_MAX,
        SIZE_MAX,
        flowq::quic::packet_protection_policy::test_allowed,
        true);

    client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.flush(at(0ms));
    auto datagram = require_single_outbound(client.drain_actions());

    server.on_datagram(flowq::quic::inbound_datagram{std::move(datagram.payload), flowq::endpoint{"client-alt", 2222, "hq-interop"}});

    auto actions = server.drain_actions();
    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::close_action>(actions[0]));
    const auto& error = std::get<flowq::quic::close_action>(actions[0]).error;
    CHECK(error.code() == flowq::error_code::protocol_error);
    CHECK(server.state() == flowq::quic::connection_loop_state::closing);
}

TEST_CASE("connection loop updates peer for subsequent sends when active migration is allowed") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);

    client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.flush(at(0ms));
    auto datagram = require_single_outbound(client.drain_actions());

    server.on_datagram(flowq::quic::inbound_datagram{std::move(datagram.payload), flowq::endpoint{"client-alt", 2222, "hq-interop"}});
    auto received = server.drain_actions();
    REQUIRE(received.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::received_packet_event>(received[0]));

    server.acknowledge(flowq::quic::packet_number_space::initial);
    auto ack = require_single_outbound(server.drain_actions());

    CHECK(ack.peer.host == "client-alt");
    CHECK(ack.peer.port == 2222);
}

TEST_CASE("connection loop resets server anti-amplification budget after peer address migration") {
    flowq::quic::plaintext_packet_protector protector{};
    flowq::endpoint client_peer{"client", 1111, "hq-interop"};
    flowq::endpoint migrated_peer{"client-alt", 2222, "hq-interop"};
    flowq::endpoint server_peer{"server", 4433, "hq-interop"};
    auto client = make_loop(cid({0x01}), cid({0x02}), server_peer, protector);

    flowq::quic::connection_loop_config server_config{};
    server_config.role = flowq::quic::connection_role::server;
    server_config.local_connection_id = cid({0x02});
    server_config.remote_connection_id = cid({0x01});
    server_config.peer = client_peer;
    server_config.initial_protector = &protector;
    server_config.handshake_protector = &protector;
    server_config.application_protector = &protector;
    server_config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    server_config.pipeline = flowq::quic::packet_pipeline_config{8192};
    server_config.peer_address_validated = true;
    auto server = flowq::quic::connection_loop{std::move(server_config)};

    client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.flush(at(0ms));
    auto migrated_datagram = require_single_outbound(client.drain_actions());
    const auto migrated_datagram_size = migrated_datagram.payload.size();

    server.on_datagram(
        flowq::quic::inbound_datagram{std::move(migrated_datagram.payload), migrated_peer},
        at(1ms));
    auto received = server.drain_actions();
    REQUIRE(received.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::received_packet_event>(received[0]));
    CHECK(std::get<flowq::quic::received_packet_event>(received[0]).peer.host == "client-alt");

    const auto oversized_response_bytes = (migrated_datagram_size * 3U) + 128U;
    server.queue_initial({
        flowq::quic::frame{flowq::quic::crypto_frame{0, text(std::string(oversized_response_bytes, 'm'))}}
    });
    server.flush(at(2ms));

    CHECK(server.drain_actions().empty());
    CHECK(server.sent_packets(flowq::quic::packet_number_space::initial).packets().empty());
}

TEST_CASE("connection loop exposes idle lifecycle timer after outbound activity") {
    flowq::quic::plaintext_packet_protector protector{};
    flowq::quic::connection_loop_config config{};
    config.role = flowq::quic::connection_role::client;
    config.local_connection_id = cid({0x01});
    config.remote_connection_id = cid({0x02});
    config.peer = flowq::endpoint{"server", 4433, "hq-interop"};
    config.initial_protector = &protector;
    config.handshake_protector = &protector;
    config.application_protector = &protector;
    config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    config.max_idle_timeout = 10ms;
    auto loop = flowq::quic::connection_loop{std::move(config)};

    loop.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.flush(at(0ms));
    (void)loop.drain_actions();

    auto timer = loop.next_lifecycle_timer(at(1ms));

    REQUIRE(timer.has_value());
    CHECK(timer->kind == flowq::quic::connection_lifecycle_timer_kind::idle);
    CHECK(timer->deadline == at(10ms));
}

TEST_CASE("connection loop closes on expired idle lifecycle timer") {
    flowq::quic::plaintext_packet_protector protector{};
    flowq::quic::connection_loop_config config{};
    config.role = flowq::quic::connection_role::client;
    config.local_connection_id = cid({0x01});
    config.remote_connection_id = cid({0x02});
    config.peer = flowq::endpoint{"server", 4433, "hq-interop"};
    config.initial_protector = &protector;
    config.handshake_protector = &protector;
    config.application_protector = &protector;
    config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    config.max_idle_timeout = 10ms;
    auto loop = flowq::quic::connection_loop{std::move(config)};

    loop.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.flush(at(0ms));
    (void)loop.drain_actions();

    loop.on_lifecycle_timer(flowq::quic::connection_lifecycle_timer_kind::idle, at(10ms));

    auto actions = loop.drain_actions();
    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::close_action>(actions[0]));
    CHECK(std::get<flowq::quic::close_action>(actions[0]).error.code() == flowq::error_code::timeout);
    CHECK(loop.state() == flowq::quic::connection_loop_state::closing);

    loop.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.flush(at(11ms));
    CHECK(loop.drain_actions().empty());
}

TEST_CASE("connection loop refreshes idle lifecycle timer after inbound activity") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    flowq::quic::connection_loop_config server_config{};
    server_config.role = flowq::quic::connection_role::server;
    server_config.local_connection_id = cid({0x02});
    server_config.remote_connection_id = cid({0x01});
    server_config.peer = flowq::endpoint{"client", 1111, "hq-interop"};
    server_config.initial_protector = &protector;
    server_config.handshake_protector = &protector;
    server_config.application_protector = &protector;
    server_config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    server_config.max_idle_timeout = 10ms;
    auto server = flowq::quic::connection_loop{std::move(server_config)};

    client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.flush(at(0ms));
    auto datagram = require_single_outbound(client.drain_actions());

    server.on_datagram(flowq::quic::inbound_datagram{std::move(datagram.payload), datagram.peer}, at(5ms));
    (void)server.drain_actions();

    auto timer = server.next_lifecycle_timer(at(6ms));
    REQUIRE(timer.has_value());
    CHECK(timer->kind == flowq::quic::connection_lifecycle_timer_kind::idle);
    CHECK(timer->deadline == at(15ms));
}

TEST_CASE("connection loop does not refresh idle lifecycle timer for ACK-only packets") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    flowq::quic::connection_loop_config server_config{};
    server_config.role = flowq::quic::connection_role::server;
    server_config.local_connection_id = cid({0x02});
    server_config.remote_connection_id = cid({0x01});
    server_config.peer = flowq::endpoint{"client", 1111, "hq-interop"};
    server_config.initial_protector = &protector;
    server_config.handshake_protector = &protector;
    server_config.application_protector = &protector;
    server_config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    server_config.max_idle_timeout = 10ms;
    auto server = flowq::quic::connection_loop{std::move(server_config)};

    client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.flush(at(0ms));
    auto datagram = require_single_outbound(client.drain_actions());
    server.on_datagram(flowq::quic::inbound_datagram{std::move(datagram.payload), datagram.peer}, at(0ms));
    (void)server.drain_actions();

    server.acknowledge(flowq::quic::packet_number_space::initial, at(5ms));
    (void)server.drain_actions();

    auto timer = server.next_lifecycle_timer(at(6ms));
    REQUIRE(timer.has_value());
    CHECK(timer->kind == flowq::quic::connection_lifecycle_timer_kind::idle);
    CHECK(timer->deadline == at(10ms));
}

TEST_CASE("connection loop expires draining lifecycle state to closed") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    flowq::quic::connection_loop_config server_config{};
    server_config.role = flowq::quic::connection_role::server;
    server_config.local_connection_id = cid({0x02});
    server_config.remote_connection_id = cid({0x01});
    server_config.peer = flowq::endpoint{"client", 1111, "hq-interop"};
    server_config.initial_protector = &protector;
    server_config.handshake_protector = &protector;
    server_config.application_protector = &protector;
    server_config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    server_config.closing_draining_timeout = 25ms;
    auto server = flowq::quic::connection_loop{std::move(server_config)};

    client.queue_initial({flowq::quic::frame{flowq::quic::connection_close_frame{0, 0, "peer closed"}}});
    client.flush(at(0ms));
    auto datagram = require_single_outbound(client.drain_actions());

    server.on_datagram(flowq::quic::inbound_datagram{std::move(datagram.payload), datagram.peer}, at(0ms));
    (void)server.drain_actions();

    auto timer = server.next_lifecycle_timer(at(0ms));
    REQUIRE(timer.has_value());
    CHECK(timer->kind == flowq::quic::connection_lifecycle_timer_kind::draining);
    CHECK(timer->deadline == at(25ms));

    server.on_lifecycle_timer(flowq::quic::connection_lifecycle_timer_kind::draining, at(25ms));

    CHECK(server.state() == flowq::quic::connection_loop_state::closed);
    CHECK_FALSE(server.next_lifecycle_timer(at(26ms)).has_value());
    auto appended = server.append_stream_data(0, text("late"));
    CHECK_FALSE(appended.ok());
    CHECK(appended.error.code() == flowq::error_code::connection_closed);
}

TEST_CASE("connection loop expires closing lifecycle state to closed") {
    flowq::quic::plaintext_packet_protector protector{};
    flowq::quic::connection_loop_config config{};
    config.role = flowq::quic::connection_role::client;
    config.local_connection_id = cid({0x01});
    config.remote_connection_id = cid({0x02});
    config.peer = flowq::endpoint{"server", 4433, "hq-interop"};
    config.initial_protector = &protector;
    config.handshake_protector = &protector;
    config.application_protector = &protector;
    config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    config.closing_draining_timeout = 25ms;
    auto loop = flowq::quic::connection_loop{std::move(config)};

    loop.on_datagram(flowq::quic::inbound_datagram{flowq::buffer{bytes({0xc0})}, flowq::endpoint{"peer", 9999, ""}}, at(0ms));
    (void)loop.drain_actions();

    auto timer = loop.next_lifecycle_timer(at(0ms));
    REQUIRE(timer.has_value());
    CHECK(timer->kind == flowq::quic::connection_lifecycle_timer_kind::closing);
    CHECK(timer->deadline == at(25ms));

    loop.on_lifecycle_timer(flowq::quic::connection_lifecycle_timer_kind::closing, at(25ms));

    CHECK(loop.state() == flowq::quic::connection_loop_state::closed);
    CHECK_FALSE(loop.next_lifecycle_timer(at(26ms)).has_value());
}

TEST_CASE("connection loop rejects test-only protection when production protection is required") {
    flowq::quic::plaintext_packet_protector protector{};
    auto loop = make_loop(
        cid({0x01}),
        cid({0x02}),
        flowq::endpoint{"server", 4433, "hq-interop"},
        protector,
        UINT64_MAX,
        UINT64_MAX,
        SIZE_MAX,
        flowq::quic::packet_protection_policy::production_required);

    loop.queue_application({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.flush(at(0ms));

    auto actions = loop.drain_actions();
    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::close_action>(actions[0]));
    CHECK_FALSE(std::get<flowq::quic::close_action>(actions[0]).error.ok());
}

TEST_CASE("connection loop blocks production Application data before TLS handshake confirmation and keys") {
    provider_backed_packet_protector initial_protector{flowq::quic::protection_level::initial};
    provider_backed_packet_protector handshake_protector{flowq::quic::protection_level::handshake};
    provider_backed_packet_protector application_protector{flowq::quic::protection_level::application};
    recording_tls_adapter adapter{};
    adapter.state_value = flowq::quic::handshake_state::handshaking;
    adapter.keys.application = true;
    flowq::quic::connection_loop_config config{};
    config.role = flowq::quic::connection_role::client;
    config.local_connection_id = cid({0x01});
    config.remote_connection_id = cid({0x02});
    config.peer = flowq::endpoint{"server", 4433, "hq-interop"};
    config.initial_protector = &initial_protector;
    config.handshake_protector = &handshake_protector;
    config.application_protector = &application_protector;
    config.protection_policy = flowq::quic::packet_protection_policy::production_required;
    config.tls_adapter = &adapter;
    auto loop = flowq::quic::connection_loop{std::move(config)};

    loop.queue_application({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.flush(at(0ms));

    auto actions = loop.drain_actions();
    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::close_action>(actions[0]));
    CHECK_FALSE(std::get<flowq::quic::close_action>(actions[0]).error.ok());
}

TEST_CASE("connection loop allows production Application data with confirmed TLS-owned key schedule") {
    provider_backed_packet_protector initial_protector{flowq::quic::protection_level::initial};
    provider_backed_packet_protector handshake_protector{flowq::quic::protection_level::handshake};
    provider_backed_packet_protector application_protector{flowq::quic::protection_level::application};
    recording_tls_adapter adapter{};
    adapter.state_value = flowq::quic::handshake_state::handshake_confirmed;
    adapter.keys.application = true;
    adapter.status = flowq::quic::crypto_provider_status::available(
        flowq::quic::cipher_suite::aes_128_gcm_sha256,
        flowq::quic::crypto_capabilities{false, false, false, false, true});

    flowq::quic::connection_loop_config config{};
    config.role = flowq::quic::connection_role::client;
    config.local_connection_id = cid({0x01});
    config.remote_connection_id = cid({0x02});
    config.peer = flowq::endpoint{"server", 4433, "hq-interop"};
    config.initial_protector = &initial_protector;
    config.handshake_protector = &handshake_protector;
    config.application_protector = &application_protector;
    config.protection_policy = flowq::quic::packet_protection_policy::production_required;
    config.tls_adapter = &adapter;
    auto loop = flowq::quic::connection_loop{std::move(config)};

    loop.queue_application({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.flush(at(0ms));

    auto actions = loop.drain_actions();
    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::outbound_datagram>(actions[0]));
    CHECK_FALSE(std::get<flowq::quic::outbound_datagram>(actions[0]).payload.empty());
}

TEST_CASE("connection loop blocks production Application data when TLS adapter lacks key schedule ownership") {
    provider_backed_packet_protector initial_protector{flowq::quic::protection_level::initial};
    provider_backed_packet_protector handshake_protector{flowq::quic::protection_level::handshake};
    provider_backed_packet_protector application_protector{flowq::quic::protection_level::application};
    recording_tls_adapter adapter{};
    adapter.state_value = flowq::quic::handshake_state::handshake_confirmed;
    adapter.keys.application = true;

    flowq::quic::connection_loop_config config{};
    config.role = flowq::quic::connection_role::client;
    config.local_connection_id = cid({0x01});
    config.remote_connection_id = cid({0x02});
    config.peer = flowq::endpoint{"server", 4433, "hq-interop"};
    config.initial_protector = &initial_protector;
    config.handshake_protector = &handshake_protector;
    config.application_protector = &application_protector;
    config.protection_policy = flowq::quic::packet_protection_policy::production_required;
    config.tls_adapter = &adapter;
    auto loop = flowq::quic::connection_loop{std::move(config)};

    loop.queue_application({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.flush(at(0ms));

    auto actions = loop.drain_actions();
    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::close_action>(actions[0]));
    CHECK_FALSE(std::get<flowq::quic::close_action>(actions[0]).error.ok());
}

TEST_CASE("connection loop tracks bytes-in-flight through congestion controller") {
    flowq::quic::plaintext_packet_protector protector{};
    auto loop = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);

    loop.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.flush(at(0ms));

    auto datagram = require_single_outbound(loop.drain_actions());
    CHECK(loop.congestion().bytes_in_flight() == datagram.payload.size());
    CHECK(loop.sent_packets(flowq::quic::packet_number_space::initial).packets().size() == 1);
}

TEST_CASE("connection loop congestion controller is path-level across packet spaces") {
    flowq::quic::plaintext_packet_protector protector{};
    auto loop = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);

    loop.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.queue_handshake({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.flush(at(0ms));

    auto actions = loop.drain_actions();
    REQUIRE(actions.size() == 2);
    const auto initial_datagram = std::get<flowq::quic::outbound_datagram>(actions[0]);
    const auto handshake_datagram = std::get<flowq::quic::outbound_datagram>(actions[1]);
    CHECK(loop.congestion().bytes_in_flight() == initial_datagram.payload.size() + handshake_datagram.payload.size());
}

TEST_CASE("connection loop reduces bytes-in-flight on ACK") {
    flowq::quic::plaintext_packet_protector protector{};
    auto client = make_loop(cid({0x01}), cid({0x02}), flowq::endpoint{"server", 4433, "hq-interop"}, protector);
    auto server = make_loop(cid({0x02}), cid({0x01}), flowq::endpoint{"client", 1111, "hq-interop"}, protector);

    client.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    client.flush(at(0ms));
    auto outbound = require_single_outbound(client.drain_actions());
    const auto bytes_before_ack = client.congestion().bytes_in_flight();
    CHECK(bytes_before_ack > 0);

    server.on_datagram(flowq::quic::inbound_datagram{std::move(outbound.payload), outbound.peer});
    (void)server.drain_actions();
    server.acknowledge(flowq::quic::packet_number_space::initial);
    auto ack_datagram = require_single_outbound(server.drain_actions());
    client.on_datagram(flowq::quic::inbound_datagram{std::move(ack_datagram.payload), ack_datagram.peer});

    CHECK(client.congestion().bytes_in_flight() < bytes_before_ack);
}

TEST_CASE("connection loop congestion window gates flush when exhausted") {
    flowq::quic::plaintext_packet_protector protector{};
    flowq::quic::connection_loop_config config{};
    config.role = flowq::quic::connection_role::client;
    config.local_connection_id = cid({0x01});
    config.remote_connection_id = cid({0x02});
    config.peer = flowq::endpoint{"server", 4433, "hq-interop"};
    config.initial_protector = &protector;
    config.handshake_protector = &protector;
    config.application_protector = &protector;
    config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    auto loop = flowq::quic::connection_loop{std::move(config)};

    // Send first packet to measure actual datagram size
    loop.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.flush(at(0ms));
    auto first = require_single_outbound(loop.drain_actions());
    const auto packet_size = first.payload.size();
    CHECK(packet_size > 0);

    // Drain all remaining bytes-in-flight (simulate ACK)
    // by sending enough packets to fill the window
    const auto window = loop.congestion().congestion_window();
    const auto packets_needed = (window / packet_size) + 1;
    for (std::size_t i = 1; i < packets_needed; ++i) {
        loop.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
        loop.flush(at(std::chrono::milliseconds{static_cast<long long>(i)}));
        (void)loop.drain_actions();
    }

    CHECK_FALSE(loop.congestion().can_send());

    // Next flush should be gated
    loop.queue_initial({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.flush(at(1000ms));
    CHECK(loop.drain_actions().empty());
}
