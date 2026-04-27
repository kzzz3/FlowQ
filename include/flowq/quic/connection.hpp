#pragma once

#include <flowq/endpoint.hpp>
#include <flowq/quic/core.hpp>
#include <flowq/quic/packet_pipeline.hpp>

#include <cstdint>
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
};

struct received_packet_event {
    packet_number number{};
    std::vector<frame> frames;
    flowq::endpoint peer;
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
          handshake_sent_{packet_number_space::handshake} {}

    void queue_initial(std::vector<frame> frames) {
        initial_queue_ = std::move(frames);
    }

    void queue_handshake(std::vector<frame> frames) {
        handshake_queue_ = std::move(frames);
    }

    void flush() {
        flush_space(packet_number_space::initial, initial_queue_, next_initial_packet_number_);
        flush_space(packet_number_space::handshake, handshake_queue_, next_handshake_packet_number_);
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
        actions_.emplace_back(received_packet_event{parsed.number, std::move(parsed.frames), std::move(datagram.peer)});
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

    void flush_space(packet_number_space space, std::vector<frame>& queue, std::uint64_t& next_packet_number) {
        if (queue.empty()) {
            return;
        }

        const auto ack_eliciting = detail::is_ack_eliciting(queue);
        auto assembled = assemble_space(space, next_packet_number, queue);
        if (!assembled.ok()) {
            actions_.emplace_back(close_action{assembled.error});
            return;
        }

        ++next_packet_number;
        sent_tracker(space).on_packet_sent(assembled.number.value, ack_eliciting);
        queue.clear();
        actions_.emplace_back(outbound_datagram{std::move(assembled.datagram), config_.peer});
    }

    void apply_ack_frames(packet_number_space space, const std::vector<frame>& frames) {
        for (const auto& item : frames) {
            if (const auto* ack = std::get_if<ack_frame>(&item)) {
                (void)sent_tracker(space).on_ack_received(*ack);
            }
        }
    }
};

} // namespace flowq::quic
