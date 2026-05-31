#include "production_quic_test_context.hpp"
#include <flowq/quic/connection.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <initializer_list>
#include <map>
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
    flowq::quic::test::production_packet_protectors& protectors,
    flowq::quic::test::confirmed_tls_adapter& tls,
    bool client_direction,
    std::uint64_t initial_stream_send_max_data = UINT64_MAX,
    std::uint64_t initial_connection_send_max_data = UINT64_MAX) {
    flowq::quic::connection_loop_config config{};
    config.role = flowq::quic::connection_role::client;
    config.local_connection_id = std::move(local);
    config.remote_connection_id = std::move(remote);
    config.peer = std::move(peer);
    config.pipeline = flowq::quic::packet_pipeline_config{1200};
    config.initial_stream_send_max_data = initial_stream_send_max_data;
    config.initial_connection_send_max_data = initial_connection_send_max_data;
    config.max_packet_payload_size = SIZE_MAX;
    config.tls_adapter = &tls;
    flowq::quic::test::mark_application_ready(config.key_lifecycle);
    config.initial_tx_protector = client_direction ? &protectors.client_initial_tx : &protectors.server_initial_tx;
    config.initial_rx_protector = client_direction ? &protectors.server_initial_tx : &protectors.client_initial_tx;
    config.handshake_tx_protector = client_direction ? &protectors.client_handshake_tx : &protectors.server_handshake_tx;
    config.handshake_rx_protector = client_direction ? &protectors.server_handshake_tx : &protectors.client_handshake_tx;
    config.application_tx_protector = client_direction ? &protectors.client_application_tx : &protectors.server_application_tx;
    config.application_rx_protector = client_direction ? &protectors.server_application_tx : &protectors.client_application_tx;
    return flowq::quic::connection_loop{std::move(config)};
}

class loopback_session {
public:
    explicit loopback_session(
        std::uint64_t client_initial_stream_send_max_data = UINT64_MAX,
        std::uint64_t client_initial_connection_send_max_data = UINT64_MAX)
        : client_{make_loop(
              cid({0x01}),
              cid({0x02}),
              flowq::endpoint{"server", 4433, "hq-interop"},
              protectors_,
              client_tls_,
              true,
              client_initial_stream_send_max_data,
              client_initial_connection_send_max_data)},
          server_{make_loop(
              cid({0x02}),
              cid({0x01}),
              flowq::endpoint{"client", 1111, "hq-interop"},
              protectors_,
              server_tls_,
              false)} {
        REQUIRE(protectors_.ok());
    }

    [[nodiscard]] flowq::quic::stream_operation_result client_append(std::uint64_t stream_id, std::string value) {
        return client_.append_stream_data(stream_id, text(std::move(value)));
    }

    [[nodiscard]] flowq::quic::stream_operation_result server_append(std::uint64_t stream_id, std::string value) {
        return server_.append_stream_data(stream_id, text(std::move(value)));
    }

    void pump_client_streams(std::initializer_list<std::uint64_t> stream_ids) {
        auto scheduled = client_.schedule_stream_frames(std::span<const std::uint64_t>{stream_ids.begin(), stream_ids.size()}, 8, 1200);
        REQUIRE(scheduled.ok());
        REQUIRE_FALSE(scheduled.frames.empty());
        client_.queue_application(std::move(scheduled.frames));
        client_.flush(at(0ms));
        pump(client_, server_, server_delivered_);
    }

    void pump_server_streams(std::initializer_list<std::uint64_t> stream_ids) {
        auto scheduled = server_.schedule_stream_frames(std::span<const std::uint64_t>{stream_ids.begin(), stream_ids.size()}, 8, 1200);
        REQUIRE(scheduled.ok());
        REQUIRE_FALSE(scheduled.frames.empty());
        server_.queue_application(std::move(scheduled.frames));
        server_.flush(at(0ms));
        pump(server_, client_, client_delivered_);
    }

    void pump_client_streams_dropped(std::initializer_list<std::uint64_t> stream_ids) {
        auto scheduled = client_.schedule_stream_frames(std::span<const std::uint64_t>{stream_ids.begin(), stream_ids.size()}, 8, 1200);
        REQUIRE(scheduled.ok());
        REQUIRE_FALSE(scheduled.frames.empty());
        client_.queue_application(std::move(scheduled.frames));
        client_.flush(at(0ms));
        drop_outbound(client_);
    }

    void pump_client_ping() {
        client_.queue_application({flowq::quic::frame{flowq::quic::ping_frame{}}});
        client_.flush(at(10ms));
        pump(client_, server_, server_delivered_);
    }

    void client_expire_application_recovery(std::chrono::milliseconds now) {
        client_.update_rtt(flowq::quic::rtt_sample{80ms, 0ms, 25ms, true});
        auto result = client_.on_recovery_timer(flowq::quic::packet_number_space::application, at(now));
        CHECK(result.newly_lost == std::vector<std::uint64_t>{0});
    }

    void server_ack_application() {
        server_.acknowledge(flowq::quic::packet_number_space::application);
        pump(server_, client_, client_delivered_);
    }

    void server_grant_stream_credit(std::uint64_t stream_id, std::uint64_t maximum_stream_data) {
        server_.queue_application({flowq::quic::frame{flowq::quic::max_stream_data_frame{stream_id, maximum_stream_data}}});
        server_.flush(at(20ms));
        pump(server_, client_, client_delivered_);
    }

    void client_reset_stream(std::uint64_t stream_id, std::uint64_t error_code, std::uint64_t final_size) {
        client_.queue_application({flowq::quic::frame{flowq::quic::reset_stream_frame{stream_id, error_code, final_size}}});
        client_.flush(at(0ms));
        pump(client_, server_, server_delivered_);
    }

    [[nodiscard]] const flowq::quic::sent_packet_tracker& client_application_packets() const {
        return client_.sent_packets(flowq::quic::packet_number_space::application);
    }

    [[nodiscard]] std::string server_delivered_text(std::uint64_t stream_id) const {
        const auto found = server_delivered_.find(stream_id);
        return found == server_delivered_.end() ? std::string{} : found->second;
    }

    [[nodiscard]] std::string client_delivered_text(std::uint64_t stream_id) const {
        const auto found = client_delivered_.find(stream_id);
        return found == client_delivered_.end() ? std::string{} : found->second;
    }

    [[nodiscard]] const flowq::quic::stream_receive_state* server_receive_stream(std::uint64_t stream_id) const noexcept {
        return server_.receive_stream(stream_id);
    }

private:
    flowq::quic::test::production_packet_protectors protectors_{flowq::quic::test::make_production_packet_protectors()};
    flowq::quic::test::confirmed_tls_adapter client_tls_{};
    flowq::quic::test::confirmed_tls_adapter server_tls_{};
    flowq::quic::connection_loop client_;
    flowq::quic::connection_loop server_;
    std::map<std::uint64_t, std::string> client_delivered_{};
    std::map<std::uint64_t, std::string> server_delivered_{};

    static void collect_deliveries(
        std::vector<flowq::quic::connection_loop_action> actions,
        std::map<std::uint64_t, std::string>& delivered) {
        for (auto& action : actions) {
            if (auto* event = std::get_if<flowq::quic::received_packet_event>(&action)) {
                for (const auto& delivery : event->stream_deliveries) {
                    REQUIRE(delivery.result.ok());
                    delivered[delivery.stream_id] += as_string(delivery.result.data);
                }
            }
        }
    }

    static void pump(
        flowq::quic::connection_loop& from,
        flowq::quic::connection_loop& to,
        std::map<std::uint64_t, std::string>& delivered) {
        auto actions = from.drain_actions();
        REQUIRE_FALSE(actions.empty());
        for (auto& action : actions) {
            REQUIRE(std::holds_alternative<flowq::quic::outbound_datagram>(action));
            auto datagram = std::get<flowq::quic::outbound_datagram>(std::move(action));
            to.on_datagram(flowq::quic::inbound_datagram{std::move(datagram.payload), datagram.peer});
        }
        collect_deliveries(to.drain_actions(), delivered);
    }

    static void drop_outbound(flowq::quic::connection_loop& from) {
        auto actions = from.drain_actions();
        REQUIRE(actions.size() == 1);
        REQUIRE(std::holds_alternative<flowq::quic::outbound_datagram>(actions[0]));
    }
};

} // namespace

TEST_CASE("loopback session delivers client stream data to server over short-header Application packets") {
    loopback_session session{};

    REQUIRE(session.client_append(0, "hello").ok());
    session.pump_client_streams({0});

    CHECK(session.server_delivered_text(0) == "hello");
}

TEST_CASE("loopback session delivers server stream data to client over short-header Application packets") {
    loopback_session session{};

    REQUIRE(session.server_append(1, "world").ok());
    session.pump_server_streams({1});

    CHECK(session.client_delivered_text(1) == "world");
}

TEST_CASE("loopback session acknowledges delivered Application stream packets") {
    loopback_session session{};

    REQUIRE(session.client_append(0, "hello").ok());
    session.pump_client_streams({0});
    session.server_ack_application();

    const auto& packets = session.client_application_packets().packets();
    REQUIRE(packets.size() == 1);
    CHECK(packets[0].state == flowq::quic::sent_packet_state::acknowledged);
}

TEST_CASE("loopback session retransmits lost stream data after deterministic Application loss") {
    loopback_session session{};

    REQUIRE(session.client_append(0, "lost").ok());
    session.pump_client_streams_dropped({0});
    CHECK(session.server_delivered_text(0).empty());

    session.pump_client_ping();
    session.server_ack_application();
    session.client_expire_application_recovery(100ms);
    session.pump_client_streams({0});

    CHECK(session.server_delivered_text(0) == "lost");
}

TEST_CASE("loopback session delivers flow-control prefix then suffix after MAX_STREAM_DATA") {
    loopback_session session{2};

    REQUIRE(session.client_append(0, "hello").ok());
    session.pump_client_streams({0});
    CHECK(session.server_delivered_text(0) == "he");

    session.server_grant_stream_credit(0, 5);
    session.pump_client_streams({0});

    CHECK(session.server_delivered_text(0) == "hello");
}

TEST_CASE("loopback session observes Application RESET_STREAM on the peer") {
    loopback_session session{};

    session.client_reset_stream(0, 0x2a, 5);

    const auto* stream = session.server_receive_stream(0);
    REQUIRE(stream != nullptr);
    CHECK(stream->reset_received());
    CHECK(stream->reset_error_code() == 0x2a);
    CHECK(stream->reset_final_size() == 5);
}
