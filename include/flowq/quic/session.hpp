#pragma once

#include <flowq/endpoint.hpp>
#include <flowq/quic/connection.hpp>
#include <flowq/quic/events.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <span>
#include <utility>

namespace flowq::quic {

/// @note Contains a connection_loop_config member for consolidated access.
struct session_config {
    connection_role role{};
    std::uint32_t version{1};
    connection_id local_connection_id;
    connection_id remote_connection_id;
    flowq::endpoint peer;
    packet_pipeline_config pipeline{};
    std::uint64_t initial_stream_send_max_data{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t initial_connection_send_max_data{std::numeric_limits<std::uint64_t>::max()};
    std::size_t max_packet_payload_size{std::numeric_limits<std::size_t>::max()};
    packet_protection_policy protection_policy{packet_protection_policy::production_required};
    std::size_t max_stream_frames{8};
    std::size_t max_stream_data_size{1200};
    std::uint64_t initial_max_stream_data_bidi_local{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t initial_max_stream_data_bidi_remote{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t initial_max_stream_data_uni{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t initial_max_streams_bidi{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t initial_max_streams_uni{std::numeric_limits<std::uint64_t>::max()};
    std::chrono::milliseconds max_idle_timeout{};
    std::uint64_t max_udp_payload_size{1200};
    std::uint64_t active_connection_id_limit{2};
    bool disable_active_migration{};
    /// @pre The adapter must outlive this session.
    tls_handshake_adapter* tls_adapter{};
    key_lifecycle_state key_lifecycle{};
    std::chrono::milliseconds closing_draining_timeout{std::chrono::seconds{3}};
    bool peer_address_validated{};
    /// @brief Consolidated connection_loop_config, populated by to_connection_config().
    connection_loop_config loop_config{};
    /// @pre The protector must outlive this session.
    const packet_protector* initial_tx_protector{};
    /// @pre The protector must outlive this session.
    const packet_protector* initial_rx_protector{};
    /// @pre The protector must outlive this session.
    const packet_protector* handshake_tx_protector{};
    /// @pre The protector must outlive this session.
    const packet_protector* handshake_rx_protector{};
    /// @pre The protector must outlive this session.
    const packet_protector* application_tx_protector{};
    /// @pre The protector must outlive this session.
    const packet_protector* application_rx_protector{};
    std::function<packet_protector_update()> packet_protector_refresh;
};

inline void apply_directional_protectors(session_config& config) noexcept {
    config.loop_config.initial_tx_protector = config.initial_tx_protector;
    config.loop_config.initial_rx_protector = config.initial_rx_protector;
    config.loop_config.handshake_tx_protector = config.handshake_tx_protector;
    config.loop_config.handshake_rx_protector = config.handshake_rx_protector;
    config.loop_config.application_tx_protector = config.application_tx_protector;
    config.loop_config.application_rx_protector = config.application_rx_protector;
    config.loop_config.packet_protector_refresh = config.packet_protector_refresh;
}

/// @brief Apply transport parameters to session_config.
/// Delegates to the connection_loop_config overload via the contained loop_config member.
inline void apply_transport_parameters(session_config& config, const transport_parameters& parameters) {
    // Populate loop_config from direct fields before applying
    config.loop_config = connection_loop_config{
        config.role,
        config.version,
        config.local_connection_id,
        config.remote_connection_id,
        config.peer,
        config.pipeline,
        config.initial_stream_send_max_data,
        config.initial_connection_send_max_data,
        config.max_packet_payload_size,
        config.protection_policy,
        config.initial_max_stream_data_bidi_local,
        config.initial_max_stream_data_bidi_remote,
        config.initial_max_stream_data_uni,
        config.initial_max_streams_bidi,
        config.initial_max_streams_uni,
        config.max_idle_timeout,
        config.max_udp_payload_size,
        config.active_connection_id_limit,
        config.disable_active_migration,
        config.tls_adapter,
        config.key_lifecycle,
        config.closing_draining_timeout,
        config.peer_address_validated
    };
    apply_directional_protectors(config);
    apply_transport_parameters(config.loop_config, parameters);
    config.max_idle_timeout = config.loop_config.max_idle_timeout;
    config.max_udp_payload_size = config.loop_config.max_udp_payload_size;
    config.max_packet_payload_size = config.loop_config.max_packet_payload_size;
    config.initial_connection_send_max_data = config.loop_config.initial_connection_send_max_data;
    config.initial_max_stream_data_bidi_local = config.loop_config.initial_max_stream_data_bidi_local;
    config.initial_max_stream_data_bidi_remote = config.loop_config.initial_max_stream_data_bidi_remote;
    config.initial_max_stream_data_uni = config.loop_config.initial_max_stream_data_uni;
    config.initial_stream_send_max_data = config.loop_config.initial_stream_send_max_data;
    config.initial_max_streams_bidi = config.loop_config.initial_max_streams_bidi;
    config.initial_max_streams_uni = config.loop_config.initial_max_streams_uni;
    config.active_connection_id_limit = config.loop_config.active_connection_id_limit;
    config.disable_active_migration = config.loop_config.disable_active_migration;
}

/// @note This class is NOT thread-safe. All methods must be called from the same thread.
class session {
public:
    explicit session(session_config config)
        : config_{std::move(config)}, loop_{to_connection_config(config_)} {}

    [[nodiscard]] stream_operation_result append_stream_data(std::uint64_t stream_id, const flowq::buffer& data) {
        return loop_.append_stream_data(stream_id, data);
    }

    [[nodiscard]] session_send_result queue_stream_data(
        std::initializer_list<std::uint64_t> stream_ids,
        std::size_t max_frames = 0,
        std::size_t max_stream_data_size = 0) {
        return queue_stream_data(
            std::span<const std::uint64_t>{stream_ids.begin(), stream_ids.size()},
            max_frames,
            max_stream_data_size);
    }

    [[nodiscard]] session_send_result queue_stream_data(
        std::span<const std::uint64_t> stream_ids,
        std::size_t max_frames = 0,
        std::size_t max_stream_data_size = 0) {
        const auto frame_limit = max_frames == 0 ? config_.max_stream_frames : max_frames;
        const auto data_limit = max_stream_data_size == 0 ? config_.max_stream_data_size : max_stream_data_size;
        auto scheduled = loop_.schedule_stream_frames(stream_ids, frame_limit, data_limit);
        if (!scheduled.ok()) {
            return {{}, {}, scheduled.error};
        }
        if (scheduled.frames.empty()) {
            return {};
        }

        loop_.queue_application(std::move(scheduled.frames));
        return {};
    }

    [[nodiscard]] session_send_result flush(std::chrono::steady_clock::time_point sent_at = std::chrono::steady_clock::now()) {
        loop_.flush(sent_at);
        return drain_send_actions();
    }

    void set_packet_protectors(
        const packet_protector* handshake_tx,
        const packet_protector* handshake_rx,
        const packet_protector* application_tx,
        const packet_protector* application_rx) noexcept {
        config_.handshake_tx_protector = handshake_tx != nullptr ? handshake_tx : config_.handshake_tx_protector;
        config_.handshake_rx_protector = handshake_rx != nullptr ? handshake_rx : config_.handshake_rx_protector;
        config_.application_tx_protector = application_tx != nullptr ? application_tx : config_.application_tx_protector;
        config_.application_rx_protector = application_rx != nullptr ? application_rx : config_.application_rx_protector;
        loop_.set_packet_protectors(handshake_tx, handshake_rx, application_tx, application_rx);
    }

    void set_remote_connection_id(connection_id remote_connection_id) {
        config_.remote_connection_id = remote_connection_id;
        loop_.set_remote_connection_id(std::move(remote_connection_id));
    }

    [[nodiscard]] session_send_result acknowledge(packet_number_space space) {
        loop_.acknowledge(space);
        return drain_send_actions();
    }

    [[nodiscard]] session_receive_result on_datagram(inbound_datagram datagram) {
        loop_.on_datagram(std::move(datagram));
        return drain_receive_actions();
    }

    [[nodiscard]] session_receive_result on_datagram(inbound_datagram datagram, std::chrono::steady_clock::time_point received_at) {
        loop_.on_datagram(std::move(datagram), received_at);
        return drain_receive_actions();
    }

    [[nodiscard]] std::optional<connection_recovery_timer> next_recovery_timer() {
        return loop_.next_recovery_timer();
    }

    [[nodiscard]] connection_recovery_result on_recovery_timer(packet_number_space space, std::chrono::steady_clock::time_point now) {
        return loop_.on_recovery_timer(space, now);
    }

    void discard_packet_space(packet_number_space space) {
        config_.key_lifecycle.discard(space);
        config_.loop_config.key_lifecycle.discard(space);
        loop_.discard_packet_space(space);
    }

    [[nodiscard]] std::optional<connection_lifecycle_timer> next_lifecycle_timer(std::chrono::steady_clock::time_point now) {
        return loop_.next_lifecycle_timer(now);
    }

    [[nodiscard]] session_send_result on_lifecycle_timer(connection_lifecycle_timer_kind kind, std::chrono::steady_clock::time_point now) {
        loop_.on_lifecycle_timer(kind, now);
        return drain_send_actions();
    }

private:
    session_config config_;
    connection_loop loop_;

    [[nodiscard]] static connection_loop_config& to_connection_config(session_config& config) {
        config.loop_config = connection_loop_config{
            config.role,
            config.version,
            config.local_connection_id,
            config.remote_connection_id,
            config.peer,
            config.pipeline,
            config.initial_stream_send_max_data,
            config.initial_connection_send_max_data,
            config.max_packet_payload_size,
            config.protection_policy,
            config.initial_max_stream_data_bidi_local,
            config.initial_max_stream_data_bidi_remote,
            config.initial_max_stream_data_uni,
            config.initial_max_streams_bidi,
            config.initial_max_streams_uni,
            config.max_idle_timeout,
            config.max_udp_payload_size,
            config.active_connection_id_limit,
            config.disable_active_migration,
            config.tls_adapter,
            config.key_lifecycle,
            config.closing_draining_timeout,
            config.peer_address_validated
        };
        apply_directional_protectors(config);
        return config.loop_config;
    }

    [[nodiscard]] session_send_result drain_send_actions() {
        session_send_result result{};
        for (auto& action : loop_.drain_actions()) {
            if (auto* datagram = std::get_if<outbound_datagram>(&action)) {
                result.datagrams.push_back(std::move(*datagram));
            } else if (auto* close = std::get_if<close_action>(&action)) {
                if (result.error.ok()) {
                    result.error = close->error;
                }
                result.closes.push_back(std::move(*close));
            }
        }
        return result;
    }

    [[nodiscard]] session_receive_result drain_receive_actions() {
        session_receive_result result{};
        for (auto& action : loop_.drain_actions()) {
            if (auto* datagram = std::get_if<outbound_datagram>(&action)) {
                result.datagrams.push_back(std::move(*datagram));
            } else if (auto* event = std::get_if<received_packet_event>(&action)) {
                append_deliveries(result, std::move(event->stream_deliveries));
            } else if (auto* close = std::get_if<close_action>(&action)) {
                if (result.error.ok()) {
                    result.error = close->error;
                }
                result.closes.push_back(std::move(*close));
            }
        }
        return result;
    }

    static void append_deliveries(session_receive_result& result, std::vector<stream_delivery> deliveries) {
        for (auto& delivery : deliveries) {
            if (result.error.ok() && !delivery.result.ok()) {
                result.error = delivery.result.error;
            }
            result.stream_deliveries.push_back(session_stream_delivery{
                delivery.stream_id,
                std::move(delivery.result.data),
                delivery.result.final_size_known,
                delivery.result.final_size,
                delivery.result.closed,
                std::move(delivery.result.error)
            });
        }
    }
};

} // namespace flowq::quic
