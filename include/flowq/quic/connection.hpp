#pragma once

#include <flowq/endpoint.hpp>
#include <flowq/quic/core.hpp>
#include <flowq/quic/packet_pipeline.hpp>
#include <flowq/quic/stream.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace flowq::quic {

enum class connection_role {
    client,
    server
};

struct connection_loop_config {
    connection_role role{};
    std::uint32_t version{1};
    connection_id local_connection_id;
    connection_id remote_connection_id;
    flowq::endpoint peer;
    const packet_protector* initial_protector{};
    const packet_protector* handshake_protector{};
    packet_pipeline_config pipeline{};
    std::uint64_t initial_stream_send_max_data{UINT64_MAX};
    std::uint64_t initial_connection_send_max_data{UINT64_MAX};
    std::size_t max_packet_payload_size{SIZE_MAX};
};

struct received_packet_event {
    packet_number number{};
    std::vector<frame> frames;
    flowq::endpoint peer;
    std::vector<stream_delivery> stream_deliveries;
};

struct connection_recovery_timer {
    packet_number_space space{};
    loss_timer_mode mode{loss_timer_mode::none};
    std::chrono::steady_clock::time_point deadline{};
};

struct connection_recovery_result {
    packet_number_space space{};
    std::vector<std::uint64_t> newly_lost;
    std::optional<std::chrono::steady_clock::time_point> next_deadline;
};

using connection_loop_action = std::variant<outbound_datagram, received_packet_event, close_action>;

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
    return space == packet_number_space::initial || space == packet_number_space::handshake;
}

[[nodiscard]] inline long_packet_type long_type_for(packet_number_space space) noexcept {
    return space == packet_number_space::handshake ? long_packet_type::handshake : long_packet_type::initial;
}

[[nodiscard]] inline const packet_protector* protector_for(packet_number_space space, const connection_loop_config& config) noexcept {
    return space == packet_number_space::handshake ? config.handshake_protector : config.initial_protector;
}

} // namespace detail

class connection_loop {
public:
    explicit connection_loop(connection_loop_config config)
        : config_{std::move(config)},
          initial_sent_{packet_number_space::initial},
          handshake_sent_{packet_number_space::handshake},
          send_streams_{config_.initial_stream_send_max_data},
          connection_send_max_data_{config_.initial_connection_send_max_data} {}

    void queue_initial(std::vector<frame> frames) {
        initial_queue_ = std::move(frames);
    }

    void queue_handshake(std::vector<frame> frames) {
        handshake_queue_ = std::move(frames);
    }

    [[nodiscard]] stream_operation_result append_stream_data(std::uint64_t stream_id, const flowq::buffer& data) {
        return send_streams_.append(stream_id, data);
    }

    [[nodiscard]] stream_frame_schedule_result schedule_stream_frames(
        std::span<const std::uint64_t> stream_ids,
        std::size_t max_frames,
        std::size_t max_stream_data_size) {
        stream_frame_schedule_result result{};
        result.frames.reserve(std::min(max_frames, stream_ids.size()));
        if (max_frames == 0) {
            return result;
        }

        for (const auto stream_id : stream_ids) {
            if (result.frames.size() == max_frames) {
                break;
            }

            const auto remaining_credit = connection_send_max_data_ - connection_data_sent_;
            if (remaining_credit == 0) {
                if (send_streams_.has_unsent_data(stream_id)) {
                    result.frames.push_back(frame{data_blocked_frame{connection_send_max_data_}});
                    break;
                }
                continue;
            }

            const auto stream_limit = std::min(max_stream_data_size, static_cast<std::size_t>(remaining_credit));
            const std::uint64_t selected_stream[]{stream_id};
            auto stream_result = send_streams_.pop_frames(selected_stream, 1, stream_limit);
            if (!stream_result.ok()) {
                result.error = stream_result.error;
                return result;
            }
            if (stream_result.frames.empty()) {
                continue;
            }

            if (const auto* stream = std::get_if<stream_frame>(&stream_result.frames.front())) {
                connection_data_sent_ += stream->data.size();
            }
            result.frames.push_back(std::move(stream_result.frames.front()));
        }
        return result;
    }

    void update_max_data(const max_data_frame& frame) noexcept {
        connection_send_max_data_ = std::max(connection_send_max_data_, frame.maximum_data);
    }

    void flush() {
        flush(std::chrono::steady_clock::now());
    }

    void flush(std::chrono::steady_clock::time_point sent_at) {
        flush_space(packet_number_space::initial, initial_queue_, next_initial_packet_number_, sent_at);
        flush_space(packet_number_space::handshake, handshake_queue_, next_handshake_packet_number_, sent_at);
    }

    void update_rtt(const rtt_sample& sample) {
        recovery_rtt_.update(sample);
    }

    [[nodiscard]] std::optional<connection_recovery_timer> next_recovery_timer(std::chrono::steady_clock::time_point now) const {
        auto selected = recovery_timer_for(packet_number_space::initial, now);
        auto handshake = recovery_timer_for(packet_number_space::handshake, now);
        if (handshake.has_value() && (!selected.has_value() || handshake->deadline < selected->deadline)) {
            selected = handshake;
        }
        return selected;
    }

    [[nodiscard]] connection_recovery_result on_recovery_timer(packet_number_space space, std::chrono::steady_clock::time_point now) {
        connection_recovery_result result{space, {}, {}};
        if (!detail::is_connection_loop_space(space)) {
            return result;
        }

        const auto largest = largest_acknowledged(space);
        if (largest.has_value()) {
            auto detected = detect_time_threshold_losses(recovery_packets_, recovery_rtt_, space, *largest, now);
            result.newly_lost = std::move(detected.newly_lost);
            for (const auto packet_number : result.newly_lost) {
                sent_tracker(space).mark_lost(packet_number);
            }
        }

        if (auto timer = recovery_timer_for(space, now)) {
            result.next_deadline = timer->deadline;
        }
        return result;
    }

    void on_datagram(inbound_datagram datagram) {
        const auto header = decode_packet_header(datagram.payload);
        if (!header.ok()) {
            actions_.emplace_back(close_action{header.error});
            return;
        }

        const auto space = packet_space_for(header.header);
        const auto* protector = detail::protector_for(space, config_);
        const auto parsed = parse_long_packet(datagram.payload, protector);
        if (!parsed.ok()) {
            actions_.emplace_back(close_action{parsed.error});
            return;
        }

        const auto newly_observed = received_tracker(parsed.number.space).observe(parsed.number.value);
        if (!newly_observed) {
            return;
        }
        apply_ack_frames(parsed.number.space, parsed.frames);
        apply_flow_control_frames(parsed.frames);
        auto stream_deliveries = receive_stream_frames(parsed.frames);
        actions_.emplace_back(received_packet_event{parsed.number, std::move(parsed.frames), std::move(datagram.peer), std::move(stream_deliveries)});
    }

    void acknowledge(packet_number_space space) {
        if (!detail::is_connection_loop_space(space)) {
            actions_.emplace_back(close_action{flowq::error{flowq::error_code::protocol_error, "unsupported packet number space for M4b connection loop"}});
            return;
        }

        const auto& tracker = received_tracker(space);
        if (tracker.empty()) {
            return;
        }

        std::vector<frame> frames;
        frames.emplace_back(tracker.to_ack_frame());
        auto packet_number_value = space == packet_number_space::handshake ? next_handshake_packet_number_ : next_initial_packet_number_;
        const auto assembled = assemble_space(space, packet_number_value, frames);
        if (!assembled.ok()) {
            actions_.emplace_back(close_action{assembled.error});
            return;
        }

        ++packet_number_value;
        if (space == packet_number_space::handshake) {
            next_handshake_packet_number_ = packet_number_value;
        } else {
            next_initial_packet_number_ = packet_number_value;
        }

        sent_tracker(space).on_packet_sent(assembled.number.value, false);
        recovery_packets_.push_back(recovery_packet{space, assembled.number.value, std::chrono::steady_clock::now(), false, sent_packet_state::outstanding});
        actions_.emplace_back(outbound_datagram{std::move(assembled.datagram), config_.peer});
    }

    [[nodiscard]] bool has_actions() const noexcept {
        return !actions_.empty();
    }

    [[nodiscard]] std::vector<connection_loop_action> drain_actions() {
        auto drained = std::move(actions_);
        actions_.clear();
        return drained;
    }

    [[nodiscard]] const sent_packet_tracker& sent_packets(packet_number_space space) const {
        if (!detail::is_connection_loop_space(space)) {
            throw std::invalid_argument{"unsupported packet number space for M4b connection loop"};
        }
        return space == packet_number_space::handshake ? handshake_sent_ : initial_sent_;
    }

private:
    connection_loop_config config_;
    std::vector<connection_loop_action> actions_{};
    std::vector<frame> initial_queue_{};
    std::vector<frame> handshake_queue_{};
    std::uint64_t next_initial_packet_number_{};
    std::uint64_t next_handshake_packet_number_{};
    received_packet_tracker initial_received_{};
    received_packet_tracker handshake_received_{};
    sent_packet_tracker initial_sent_;
    sent_packet_tracker handshake_sent_;
    stream_receive_set receive_streams_{};
    stream_send_set send_streams_{};
    std::uint64_t connection_send_max_data_{UINT64_MAX};
    std::uint64_t connection_data_sent_{};
    rtt_estimator recovery_rtt_{};
    pto_config recovery_pto_config_{};
    std::vector<recovery_packet> recovery_packets_{};
    std::optional<std::uint64_t> largest_initial_acknowledged_{};
    std::optional<std::uint64_t> largest_handshake_acknowledged_{};

    [[nodiscard]] static packet_number_space packet_space_for(const packet_header& header) noexcept {
        return std::holds_alternative<handshake_header>(header) ? packet_number_space::handshake : packet_number_space::initial;
    }

    [[nodiscard]] received_packet_tracker& received_tracker(packet_number_space space) noexcept {
        return space == packet_number_space::handshake ? handshake_received_ : initial_received_;
    }

    [[nodiscard]] const received_packet_tracker& received_tracker(packet_number_space space) const noexcept {
        return space == packet_number_space::handshake ? handshake_received_ : initial_received_;
    }

    [[nodiscard]] sent_packet_tracker& sent_tracker(packet_number_space space) noexcept {
        return space == packet_number_space::handshake ? handshake_sent_ : initial_sent_;
    }

    [[nodiscard]] assembled_packet assemble_space(packet_number_space space, std::uint64_t packet_number_value, const std::vector<frame>& frames) const {
        return assemble_long_packet(packet_build_request{
            detail::long_type_for(space),
            config_.version,
            config_.remote_connection_id,
            config_.local_connection_id,
            {},
            packet_number{space, packet_number_value},
            frames,
            detail::protector_for(space, config_),
            config_.pipeline
        });
    }

    void flush_space(packet_number_space space, std::vector<frame>& queue, std::uint64_t& next_packet_number, std::chrono::steady_clock::time_point sent_at) {
        if (queue.empty()) {
            return;
        }

        auto selected = select_frames_for_payload_budget(queue, config_.max_packet_payload_size);
        if (!selected.ok()) {
            actions_.emplace_back(close_action{selected.error});
            return;
        }
        if (selected.frames.empty()) {
            actions_.emplace_back(close_action{flowq::error{flowq::error_code::protocol_error, "frame exceeds packet payload budget"}});
            return;
        }

        const auto ack_eliciting = detail::is_ack_eliciting(selected.frames);
        auto assembled = assemble_space(space, next_packet_number, selected.frames);
        if (!assembled.ok()) {
            actions_.emplace_back(close_action{assembled.error});
            return;
        }

        ++next_packet_number;
        sent_tracker(space).on_packet_sent(assembled.number.value, ack_eliciting);
        recovery_packets_.push_back(recovery_packet{space, assembled.number.value, sent_at, ack_eliciting, sent_packet_state::outstanding});
        queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(selected.next_index));
        actions_.emplace_back(outbound_datagram{std::move(assembled.datagram), config_.peer});
    }

    void apply_ack_frames(packet_number_space space, const std::vector<frame>& frames) {
        for (const auto& item : frames) {
            if (const auto* ack = std::get_if<ack_frame>(&item)) {
                record_largest_acknowledged(space, ack->largest_acknowledged);
                auto result = sent_tracker(space).on_ack_received(*ack);
                mark_recovery_packets(space, result.newly_acknowledged, sent_packet_state::acknowledged);
                mark_recovery_packets(space, result.newly_lost, sent_packet_state::lost);
            }
        }
    }

    [[nodiscard]] std::optional<connection_recovery_timer> recovery_timer_for(packet_number_space space, std::chrono::steady_clock::time_point now) const {
        std::optional<std::chrono::steady_clock::time_point> last_ack_eliciting_sent_at;
        std::optional<std::chrono::steady_clock::time_point> earliest_loss_time;
        const auto largest = largest_acknowledged(space);
        const auto loss_delay = recovery_rtt_.has_sample() ? time_threshold_loss_delay(recovery_rtt_) : std::chrono::steady_clock::duration{};

        for (const auto& packet : recovery_packets_) {
            if (packet.space != space || packet.state != sent_packet_state::outstanding || !packet.ack_eliciting) {
                continue;
            }
            if (!last_ack_eliciting_sent_at.has_value() || packet.sent_at > *last_ack_eliciting_sent_at) {
                last_ack_eliciting_sent_at = packet.sent_at;
            }
            if (largest.has_value() && packet.packet_number < *largest && recovery_rtt_.has_sample()) {
                const auto loss_time = packet.sent_at + loss_delay;
                if (!earliest_loss_time.has_value() || loss_time < *earliest_loss_time) {
                    earliest_loss_time = loss_time;
                }
            }
        }

        if (earliest_loss_time.has_value()) {
            return connection_recovery_timer{space, loss_timer_mode::loss_time, *earliest_loss_time};
        }
        if (last_ack_eliciting_sent_at.has_value() && pto_allowed(space, recovery_pto_config_)) {
            return connection_recovery_timer{space, loss_timer_mode::pto, pto_deadline(*last_ack_eliciting_sent_at, recovery_rtt_, space, recovery_pto_config_)};
        }
        return std::nullopt;
    }

    void record_largest_acknowledged(packet_number_space space, std::uint64_t packet_number) noexcept {
        auto& stored = space == packet_number_space::handshake ? largest_handshake_acknowledged_ : largest_initial_acknowledged_;
        if (!stored.has_value() || packet_number > *stored) {
            stored = packet_number;
        }
    }

    [[nodiscard]] std::optional<std::uint64_t> largest_acknowledged(packet_number_space space) const noexcept {
        return space == packet_number_space::handshake ? largest_handshake_acknowledged_ : largest_initial_acknowledged_;
    }

    void mark_recovery_packets(packet_number_space space, const std::vector<std::uint64_t>& packet_numbers, sent_packet_state state) noexcept {
        for (auto& packet : recovery_packets_) {
            if (packet.space != space) {
                continue;
            }
            if (std::find(packet_numbers.begin(), packet_numbers.end(), packet.packet_number) != packet_numbers.end()) {
                packet.state = state;
            }
        }
    }

    void apply_flow_control_frames(const std::vector<frame>& frames) {
        for (const auto& item : frames) {
            if (const auto* credit = std::get_if<max_stream_data_frame>(&item)) {
                send_streams_.update_max_data(*credit);
            } else if (const auto* credit = std::get_if<max_data_frame>(&item)) {
                update_max_data(*credit);
            }
        }
    }

    [[nodiscard]] std::vector<stream_delivery> receive_stream_frames(const std::vector<frame>& frames) {
        std::vector<stream_delivery> deliveries;
        for (const auto& item : frames) {
            if (const auto* stream = std::get_if<stream_frame>(&item)) {
                deliveries.push_back(receive_streams_.receive(*stream));
            }
        }
        return deliveries;
    }
};

} // namespace flowq::quic
