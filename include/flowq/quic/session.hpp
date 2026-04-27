#pragma once

#include <flowq/endpoint.hpp>
#include <flowq/quic/connection.hpp>
#include <flowq/quic/events.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <utility>

namespace flowq::quic {

struct session_config {
    connection_role role{};
    std::uint32_t version{1};
    connection_id local_connection_id;
    connection_id remote_connection_id;
    flowq::endpoint peer;
    const packet_protector* initial_protector{};
    const packet_protector* handshake_protector{};
    const packet_protector* application_protector{};
    packet_pipeline_config pipeline{};
    std::uint64_t initial_stream_send_max_data{UINT64_MAX};
    std::uint64_t initial_connection_send_max_data{UINT64_MAX};
    std::size_t max_packet_payload_size{SIZE_MAX};
    packet_protection_policy protection_policy{packet_protection_policy::test_allowed};
    std::size_t max_stream_frames{8};
    std::size_t max_stream_data_size{1200};
};

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

    [[nodiscard]] session_send_result acknowledge(packet_number_space space) {
        loop_.acknowledge(space);
        return drain_send_actions();
    }

    [[nodiscard]] session_receive_result on_datagram(inbound_datagram datagram) {
        loop_.on_datagram(std::move(datagram));
        return drain_receive_actions();
    }

    [[nodiscard]] std::optional<connection_recovery_timer> next_recovery_timer(std::chrono::steady_clock::time_point now) const {
        return loop_.next_recovery_timer(now);
    }

    [[nodiscard]] connection_recovery_result on_recovery_timer(packet_number_space space, std::chrono::steady_clock::time_point now) {
        return loop_.on_recovery_timer(space, now);
    }

private:
    session_config config_;
    connection_loop loop_;

    [[nodiscard]] static connection_loop_config to_connection_config(const session_config& config) {
        return connection_loop_config{
            config.role,
            config.version,
            config.local_connection_id,
            config.remote_connection_id,
            config.peer,
            config.initial_protector,
            config.handshake_protector,
            config.application_protector,
            config.pipeline,
            config.initial_stream_send_max_data,
            config.initial_connection_send_max_data,
            config.max_packet_payload_size,
            config.protection_policy
        };
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
