#pragma once

#include <flowq/endpoint.hpp>
#include <flowq/quic/core.hpp>
#include <flowq/quic/key_lifecycle.hpp>
#include <flowq/quic/packet_pipeline.hpp>
#include <flowq/quic/stream.hpp>
#include <flowq/quic/tls_handshake.hpp>
#include <flowq/quic/transport_parameters.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <variant>
#include <vector>

namespace flowq::quic {

// Connection roles
enum class connection_role {
    client,
    server
};

enum class connection_loop_state {
    active,
    closing,
    draining,
    closed
};

enum class connection_lifecycle_timer_kind {
    idle,
    closing,
    draining
};

using path_challenge_token = std::array<std::byte, 8>;
using path_challenge_generator = std::function<path_challenge_token()>;

struct packet_protector_update {
    flowq::error error{};
    const packet_protector* handshake_tx{};
    const packet_protector* handshake_rx{};
    const packet_protector* application_tx{};
    const packet_protector* application_rx{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

// Connection loop configuration
struct connection_loop_config {
    connection_role role{};
    std::uint32_t version{1};
    connection_id local_connection_id;
    connection_id remote_connection_id;
    flowq::endpoint peer;
    packet_pipeline_config pipeline{};
    std::uint64_t initial_stream_send_max_data{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t initial_connection_send_max_data{std::numeric_limits<std::uint64_t>::max()};
    std::size_t max_packet_payload_size{std::numeric_limits<std::size_t>::max()};
    std::uint64_t initial_max_stream_data_bidi_local{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t initial_max_stream_data_bidi_remote{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t initial_max_stream_data_uni{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t initial_max_streams_bidi{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t initial_max_streams_uni{std::numeric_limits<std::uint64_t>::max()};
    std::chrono::milliseconds max_idle_timeout{};
    std::uint64_t max_udp_payload_size{1200};
    std::uint64_t active_connection_id_limit{2};
    std::uint64_t ack_delay_exponent{3};
    std::chrono::milliseconds max_ack_delay{25};
    bool disable_active_migration{};
    /// @pre The adapter must outlive this connection loop.
    tls_handshake_adapter* tls_adapter{};
    key_lifecycle_state key_lifecycle{};
    std::chrono::milliseconds closing_draining_timeout{std::chrono::seconds{3}};
    bool peer_address_validated{};
    path_challenge_generator path_challenge;
    /// @pre The protector must outlive this connection loop.
    const packet_protector* initial_tx_protector{};
    /// @pre The protector must outlive this connection loop.
    const packet_protector* initial_rx_protector{};
    /// @pre The protector must outlive this connection loop.
    const packet_protector* handshake_tx_protector{};
    /// @pre The protector must outlive this connection loop.
    const packet_protector* handshake_rx_protector{};
    /// @pre The protector must outlive this connection loop.
    const packet_protector* application_tx_protector{};
    /// @pre The protector must outlive this connection loop.
    const packet_protector* application_rx_protector{};
    std::function<packet_protector_update()> packet_protector_refresh;
};

// Apply transport parameters to connection config
inline void apply_transport_parameters(connection_loop_config& config, const transport_parameters& parameters) {
    if (parameters.max_idle_timeout.has_value()) {
        config.max_idle_timeout = std::chrono::milliseconds{*parameters.max_idle_timeout};
    }
    if (parameters.max_udp_payload_size.has_value()) {
        config.max_udp_payload_size = *parameters.max_udp_payload_size;
        if (*parameters.max_udp_payload_size <= std::numeric_limits<std::size_t>::max()) {
            config.max_packet_payload_size = static_cast<std::size_t>(*parameters.max_udp_payload_size);
        }
    }
    if (parameters.initial_max_data.has_value()) {
        config.initial_connection_send_max_data = *parameters.initial_max_data;
    }
    if (parameters.initial_max_stream_data_bidi_local.has_value()) {
        config.initial_max_stream_data_bidi_local = *parameters.initial_max_stream_data_bidi_local;
    }
    if (parameters.initial_max_stream_data_bidi_remote.has_value()) {
        config.initial_max_stream_data_bidi_remote = *parameters.initial_max_stream_data_bidi_remote;
    }
    if (parameters.initial_max_stream_data_uni.has_value()) {
        config.initial_max_stream_data_uni = *parameters.initial_max_stream_data_uni;
        config.initial_stream_send_max_data = *parameters.initial_max_stream_data_uni;
    }
    if (parameters.initial_max_streams_bidi.has_value()) {
        config.initial_max_streams_bidi = *parameters.initial_max_streams_bidi;
    }
    if (parameters.initial_max_streams_uni.has_value()) {
        config.initial_max_streams_uni = *parameters.initial_max_streams_uni;
    }
    if (parameters.active_connection_id_limit.has_value()) {
        config.active_connection_id_limit = *parameters.active_connection_id_limit;
    }
    if (parameters.ack_delay_exponent.has_value()) {
        config.ack_delay_exponent = *parameters.ack_delay_exponent;
    }
    if (parameters.max_ack_delay.has_value()) {
        config.max_ack_delay = std::chrono::milliseconds{*parameters.max_ack_delay};
    }
    config.disable_active_migration = parameters.disable_active_migration;
}

// Received packet event
struct received_packet_event {
    packet_number number{};
    std::vector<frame> frames;
    flowq::endpoint peer;
    std::vector<stream_delivery> stream_deliveries;
};

// Connection recovery timer
struct connection_recovery_timer {
    packet_number_space space{};
    loss_timer_mode mode{loss_timer_mode::none};
    std::chrono::steady_clock::time_point deadline{};
};

struct connection_lifecycle_timer {
    connection_lifecycle_timer_kind kind{connection_lifecycle_timer_kind::idle};
    std::chrono::steady_clock::time_point deadline{};
};

// Connection recovery result
struct connection_recovery_result {
    packet_number_space space{};
    std::vector<std::uint64_t> newly_lost;
    std::optional<std::chrono::steady_clock::time_point> next_deadline;
};

// Sent stream range
struct sent_stream_range {
    std::uint64_t stream_id{};
    stream_send_range range{};
};

// Sent packet stream ranges
struct sent_packet_stream_ranges {
    packet_number_space space{};
    std::uint64_t packet_number{};
    std::vector<sent_stream_range> ranges;
};

// Connection loop action variant
using connection_loop_action = std::variant<outbound_datagram, received_packet_event, close_action>;

// Detail namespace with helper functions
namespace detail {

[[nodiscard]] inline bool is_ack_eliciting(const std::vector<frame>& frames) noexcept {
    for (const auto& item : frames) {
        if (!std::holds_alternative<padding_frame>(item) && !std::holds_alternative<ack_frame>(item)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool is_connection_loop_space(packet_number_space space) noexcept {
    return space == packet_number_space::initial || space == packet_number_space::handshake || space == packet_number_space::application;
}

[[nodiscard]] inline long_packet_type long_type_for(packet_number_space space) noexcept {
    return space == packet_number_space::handshake ? long_packet_type::handshake : long_packet_type::initial;
}

[[nodiscard]] inline const packet_protector* tx_protector_for(packet_number_space space, const connection_loop_config& config) noexcept {
    if (space == packet_number_space::application) {
        return config.application_tx_protector;
    }
    if (space == packet_number_space::handshake) {
        return config.handshake_tx_protector;
    }
    return config.initial_tx_protector;
}

[[nodiscard]] inline const packet_protector* rx_protector_for(packet_number_space space, const connection_loop_config& config) noexcept {
    if (space == packet_number_space::application) {
        return config.application_rx_protector;
    }
    if (space == packet_number_space::handshake) {
        return config.handshake_rx_protector;
    }
    return config.initial_rx_protector;
}

[[nodiscard]] inline tls_encryption_level tls_level_for(packet_number_space space) noexcept {
    if (space == packet_number_space::application) {
        return tls_encryption_level::application;
    }
    return space == packet_number_space::handshake ? tls_encryption_level::handshake : tls_encryption_level::initial;
}

[[nodiscard]] inline packet_number_space packet_space_for(tls_encryption_level level) noexcept {
    if (level == tls_encryption_level::application) {
        return packet_number_space::application;
    }
    return level == tls_encryption_level::handshake ? packet_number_space::handshake : packet_number_space::initial;
}

} // namespace detail

} // namespace flowq::quic
