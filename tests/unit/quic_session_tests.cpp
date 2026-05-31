#include "plaintext_packet_protector.hpp"
#include <flowq/quic/session.hpp>
#include <flowq/quic/tls_handshake.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>
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
        return {};
    }

    std::vector<flowq::quic::crypto_bytes> drain_crypto() override {
        auto output = std::move(outbound);
        outbound.clear();
        return output;
    }

    flowq::error advance() override {
        return {};
    }

    flowq::quic::handshake_state state_value{flowq::quic::handshake_state::idle};
    flowq::quic::tls_key_availability keys{};
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
    config.initial_tx_protector = &protector;
    config.initial_rx_protector = &protector;
    config.handshake_tx_protector = &protector;
    config.handshake_rx_protector = &protector;
    config.application_tx_protector = &protector;
    config.application_rx_protector = &protector;
    config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    return config;
}

flowq::quic::connection_loop make_loop(
    flowq::quic::connection_id local,
    flowq::quic::connection_id remote,
    flowq::endpoint peer,
    const flowq::quic::packet_protector& protector) {
    flowq::quic::connection_loop_config config{};
    config.role = flowq::quic::connection_role::client;
    config.local_connection_id = std::move(local);
    config.remote_connection_id = std::move(remote);
    config.peer = std::move(peer);
    config.pipeline = flowq::quic::packet_pipeline_config{1200};
    config.initial_tx_protector = &protector;
    config.initial_rx_protector = &protector;
    config.handshake_tx_protector = &protector;
    config.handshake_rx_protector = &protector;
    config.application_tx_protector = &protector;
    config.application_rx_protector = &protector;
    config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    return flowq::quic::connection_loop{std::move(config)};
}

flowq::quic::outbound_datagram require_single_outbound(std::vector<flowq::quic::connection_loop_action> actions) {
    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::outbound_datagram>(actions[0]));
    return std::get<flowq::quic::outbound_datagram>(std::move(actions[0]));
}

} // namespace

TEST_CASE("QUIC session public header exposes basic client configuration") {
    flowq::quic::session_config config{};
    config.role = flowq::quic::connection_role::client;

    CHECK(config.version == 1);
    CHECK(config.protection_policy == flowq::quic::packet_protection_policy::production_required);
    CHECK_FALSE(config.disable_active_migration);
    CHECK(config.active_connection_id_limit == 2);
    CHECK(config.tls_adapter == nullptr);
}

TEST_CASE("QUIC session blocks production Application data before TLS handshake confirmation and keys") {
    provider_backed_packet_protector initial_protector{flowq::quic::protection_level::initial};
    provider_backed_packet_protector handshake_protector{flowq::quic::protection_level::handshake};
    provider_backed_packet_protector application_protector{flowq::quic::protection_level::application};
    recording_tls_adapter adapter{};
    adapter.state_value = flowq::quic::handshake_state::handshake_confirmed;
    adapter.keys.application = false;
    auto config = make_config(
        cid({0x01}),
        cid({0x02}),
        flowq::endpoint{"server", 4433, "hq-interop"},
        application_protector);
    config.initial_tx_protector = &initial_protector;
    config.initial_rx_protector = &initial_protector;
    config.handshake_tx_protector = &handshake_protector;
    config.handshake_rx_protector = &handshake_protector;
    config.protection_policy = flowq::quic::packet_protection_policy::production_required;
    config.tls_adapter = &adapter;
    auto session = flowq::quic::session{std::move(config)};

    REQUIRE(session.append_stream_data(0, text("hello")).ok());
    REQUIRE(session.queue_stream_data({0}).ok());
    auto result = session.flush(at(0ms));

    CHECK_FALSE(result.ok());
    REQUIRE(result.closes.size() == 1);
}

TEST_CASE("QUIC session queues stream data before flush returns outbound Application datagrams") {
    flowq::quic::test::plaintext_packet_protector protector{};
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
    flowq::quic::test::plaintext_packet_protector protector{};
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

    auto received = server.on_datagram(flowq::quic::inbound_datagram{std::move(sent.datagrams[0].payload), flowq::endpoint{"client", 1111, "hq-interop"}});

    REQUIRE(received.ok());
    REQUIRE(received.stream_deliveries.size() == 1);
    REQUIRE(received.stream_deliveries[0].ok());
    CHECK(received.stream_deliveries[0].stream_id == 0);
    CHECK(as_string(received.stream_deliveries[0].data) == "hello");
}

TEST_CASE("QUIC session acknowledges received Application datagrams") {
    flowq::quic::test::plaintext_packet_protector protector{};
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
    REQUIRE(server.on_datagram(flowq::quic::inbound_datagram{std::move(sent.datagrams[0].payload), flowq::endpoint{"client", 1111, "hq-interop"}}).ok());

    auto acked = server.acknowledge(flowq::quic::packet_number_space::application);

    REQUIRE(acked.ok());
    REQUIRE(acked.datagrams.size() == 1);
    CHECK(acked.datagrams[0].peer.host == "client");
}

TEST_CASE("QUIC session forwards packet-space discard gates") {
    flowq::quic::test::plaintext_packet_protector protector{};
    flowq::quic::key_lifecycle_state lifecycle{};
    lifecycle.discard(flowq::quic::packet_number_space::initial);

    auto config = make_config(
        cid({0x01}),
        cid({0x02}),
        flowq::endpoint{"server", 4433, "hq-interop"},
        protector);
    config.key_lifecycle = lifecycle;
    auto session = flowq::quic::session{std::move(config)};

    auto acked = session.acknowledge(flowq::quic::packet_number_space::initial);

    REQUIRE(acked.ok());
    CHECK(acked.datagrams.empty());
    CHECK(acked.closes.empty());
}

TEST_CASE("QUIC session rejects stream work after peer connection close") {
    flowq::quic::test::plaintext_packet_protector protector{};
    auto peer = make_loop(
        cid({0x01}),
        cid({0x02}),
        flowq::endpoint{"server", 4433, "hq-interop"},
        protector);
    auto session = flowq::quic::session{make_config(
        cid({0x02}),
        cid({0x01}),
        flowq::endpoint{"client", 1111, "hq-interop"},
        protector)};

    peer.queue_initial({flowq::quic::frame{flowq::quic::connection_close_frame{0, 0, "peer closed"}}});
    peer.flush(at(0ms));
    auto datagram = require_single_outbound(peer.drain_actions());

    auto received = session.on_datagram(flowq::quic::inbound_datagram{std::move(datagram.payload), datagram.peer});
    CHECK_FALSE(received.ok());
    REQUIRE(received.closes.size() == 1);
    CHECK(received.error.code() == flowq::error_code::connection_closed);

    auto appended = session.append_stream_data(0, text("late"));
    CHECK_FALSE(appended.ok());
    CHECK(appended.error.code() == flowq::error_code::connection_closed);

    auto queued = session.queue_stream_data({0});
    CHECK_FALSE(queued.ok());
    CHECK(queued.error.code() == flowq::error_code::connection_closed);

    auto flushed = session.flush(at(1ms));
    CHECK(flushed.ok());
    CHECK(flushed.datagrams.empty());
    CHECK(flushed.closes.empty());
}

TEST_CASE("QUIC session exposes lifecycle timer and returns idle timeout close") {
    flowq::quic::test::plaintext_packet_protector protector{};
    auto config = make_config(
        cid({0x01}),
        cid({0x02}),
        flowq::endpoint{"server", 4433, "hq-interop"},
        protector);
    config.max_idle_timeout = 10ms;
    auto session = flowq::quic::session{std::move(config)};

    REQUIRE(session.append_stream_data(0, text("hello")).ok());
    REQUIRE(session.queue_stream_data({0}).ok());
    REQUIRE(session.flush(at(0ms)).ok());

    auto timer = session.next_lifecycle_timer(at(1ms));
    REQUIRE(timer.has_value());
    CHECK(timer->kind == flowq::quic::connection_lifecycle_timer_kind::idle);
    CHECK(timer->deadline == at(10ms));

    auto expired = session.on_lifecycle_timer(timer->kind, timer->deadline);

    CHECK_FALSE(expired.ok());
    REQUIRE(expired.closes.size() == 1);
    CHECK(expired.error.code() == flowq::error_code::timeout);
}

TEST_CASE("QUIC session does not arm Application recovery timer before handshake confirmation") {
    flowq::quic::test::plaintext_packet_protector protector{};
    auto session = flowq::quic::session{make_config(
        cid({0x01}),
        cid({0x02}),
        flowq::endpoint{"server", 4433, "hq-interop"},
        protector)};

    REQUIRE(session.append_stream_data(0, text("hello")).ok());
    REQUIRE(session.queue_stream_data({0}).ok());
    REQUIRE(session.flush(at(0ms)).ok());

    auto timer = session.next_recovery_timer();

    CHECK_FALSE(timer.has_value());
}
