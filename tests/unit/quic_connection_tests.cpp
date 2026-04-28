#include <flowq/quic/connection.hpp>
#include <flowq/quic/tls_handshake.hpp>

#include <catch2/catch_test_macros.hpp>

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
        if (confirm_handshake_on_drain) {
            state_value = flowq::quic::handshake_state::handshake_confirmed;
            keys.handshake = true;
            keys.application = true;
        }
        return output;
    }

    flowq::quic::handshake_state state_value{flowq::quic::handshake_state::idle};
    flowq::quic::tls_key_availability keys{};
    bool install_handshake_keys_on_receive{};
    bool confirm_handshake_on_receive{};
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
    flowq::quic::packet_protection_policy protection_policy = flowq::quic::packet_protection_policy::test_allowed) {
    return flowq::quic::connection_loop{flowq::quic::connection_loop_config{
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
    confirmed_config.tls_adapter = &confirmed_adapter;
    auto confirmed_loop = flowq::quic::connection_loop{std::move(confirmed_config)};

    confirmed_loop.queue_handshake({flowq::quic::frame{flowq::quic::ping_frame{}}});
    confirmed_loop.flush(at(0ms));

    CHECK(confirmed_loop.drain_actions().empty());
    CHECK(confirmed_loop.sent_packets(flowq::quic::packet_number_space::handshake).packets().empty());
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
    server_config.tls_adapter = &adapter;
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
    config.tls_adapter = &adapter;
    auto loop = flowq::quic::connection_loop{std::move(config)};

    loop.queue_handshake({flowq::quic::frame{flowq::quic::ping_frame{}}});
    loop.flush(at(0ms));

    CHECK(loop.drain_actions().empty());
    CHECK(loop.sent_packets(flowq::quic::packet_number_space::handshake).packets().empty());
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
    config.tls_adapter = &adapter;
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
