#pragma once

/// @note Inspection methods (sent_packets, sent_stream_ranges, congestion, receive_stream, send_stream)
/// require FLOWQ_ENABLE_INSPECTION to be defined at build time.

#include <flowq/quic/connection_packet_spaces.hpp>
#include <flowq/quic/connection_types.hpp>
#include <flowq/quic/congestion.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace flowq::quic {

/// @note This class is NOT thread-safe. All methods must be called from the same thread.
class connection_loop {
public:
    explicit connection_loop(connection_loop_config config)
        : config_{std::move(config)},
          receive_streams_{
              config_.initial_max_stream_data_bidi_local,
              local_stream_initiator(config_.role),
              stream_limits{config_.initial_max_streams_bidi, config_.initial_max_streams_uni}},
          send_streams_{
              config_.initial_stream_send_max_data,
              stream_limits{config_.initial_max_streams_bidi, config_.initial_max_streams_uni}},
          connection_send_max_data_{config_.initial_connection_send_max_data},
          peer_address_validated_{config_.peer_address_validated} {}

    connection_loop(connection_loop&&) noexcept = default;
    connection_loop& operator=(connection_loop&&) noexcept = default;

    void set_packet_protectors(
        const packet_protector* handshake_tx,
        const packet_protector* handshake_rx,
        const packet_protector* application_tx,
        const packet_protector* application_rx) noexcept {
        if (handshake_tx != nullptr) {
            config_.handshake_tx_protector = handshake_tx;
        }
        if (handshake_rx != nullptr) {
            config_.handshake_rx_protector = handshake_rx;
        }
        if (application_tx != nullptr) {
            config_.application_tx_protector = application_tx;
        }
        if (application_rx != nullptr) {
            config_.application_rx_protector = application_rx;
        }
    }

    void set_remote_connection_id(connection_id remote_connection_id) {
        config_.remote_connection_id = std::move(remote_connection_id);
    }

    void queue_initial(std::vector<frame> frames) {
        if (!active()) {
            return;
        }
        packet_spaces_.get(packet_number_space::initial).queue = std::move(frames);
    }

    void queue_handshake(std::vector<frame> frames) {
        if (!active()) {
            return;
        }
        packet_spaces_.get(packet_number_space::handshake).queue = std::move(frames);
    }

    void queue_application(std::vector<frame> frames) {
        if (!active()) {
            return;
        }
        packet_spaces_.get(packet_number_space::application).queue = std::move(frames);
    }

    [[nodiscard]] stream_operation_result append_stream_data(std::uint64_t stream_id, const flowq::buffer& data) {
        if (!active()) {
            return {flowq::error{flowq::error_code::connection_closed, "connection is closed"}};
        }
        return send_streams_.append(stream_id, data);
    }

    [[nodiscard]] stream_frame_schedule_result schedule_stream_frames(
        std::span<const std::uint64_t> stream_ids,
        std::size_t max_frames,
        std::size_t max_stream_data_size) {
        stream_frame_schedule_result result{};
        if (!active()) {
            result.error = flowq::error{flowq::error_code::connection_closed, "connection is closed"};
            return result;
        }
        result.frames.reserve(std::min(max_frames, stream_ids.size()));
        if (max_frames == 0) {
            return result;
        }

        for (const auto stream_id : stream_ids) {
            if (result.frames.size() == max_frames) {
                break;
            }

            const auto has_retransmission = send_streams_.has_retransmittable_data(stream_id);
            const auto remaining_credit = connection_send_max_data_ - connection_data_sent_;
            if (remaining_credit == 0 && !has_retransmission) {
                if (send_streams_.has_unsent_data(stream_id)) {
                    result.frames.push_back(frame{data_blocked_frame{connection_send_max_data_}});
                    break;
                }
                continue;
            }

            const auto stream_limit = has_retransmission ? max_stream_data_size : std::min(max_stream_data_size, static_cast<std::size_t>(remaining_credit));
            auto stream_result = send_streams_.pop_frame(stream_id, stream_limit);
            if (!stream_result.ok()) {
                result.error = stream_result.error;
                return result;
            }
            if (stream_result.has_frame) {
                if (!stream_result.retransmission) {
                    connection_data_sent_ += stream_result.frame.data.size();
                }
                result.frames.push_back(frame{std::move(stream_result.frame)});
                continue;
            }

            auto blocked = send_streams_.blocked_frame(stream_id);
            if (blocked.has_value()) {
                result.frames.push_back(frame{*blocked});
            }
        }
        if (result.frames.size() < max_frames) {
            if (auto blocked = send_streams_.streams_blocked_frame(stream_direction::bidirectional); blocked.has_value()) {
                result.frames.push_back(frame{*blocked});
            }
        }
        if (result.frames.size() < max_frames) {
            if (auto blocked = send_streams_.streams_blocked_frame(stream_direction::unidirectional); blocked.has_value()) {
                result.frames.push_back(frame{*blocked});
            }
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
        if (!active()) {
            return;
        }
        refresh_key_lifecycle();
        if (!active()) {
            return;
        }
        pump_tls_crypto();
        refresh_key_lifecycle();
        if (!active()) {
            return;
        }
        flush_space(packet_number_space::initial, sent_at);
        if (!active()) {
            return;
        }
        flush_space(packet_number_space::handshake, sent_at);
        if (!active()) {
            return;
        }
        flush_space(packet_number_space::application, sent_at);
    }

    void update_rtt(const rtt_sample& sample) {
        recovery_rtt_.update(sample);
    }

    [[nodiscard]] std::optional<connection_recovery_timer> next_recovery_timer() {
        if (!active()) {
            return std::nullopt;
        }
        refresh_key_lifecycle();
        if (!active()) {
            return std::nullopt;
        }
        auto selected = recovery_timer_for(packet_number_space::initial);
        auto handshake = recovery_timer_for(packet_number_space::handshake);
        if (handshake.has_value() && (!selected.has_value() || handshake->deadline < selected->deadline)) {
            selected = handshake;
        }
        auto application = recovery_timer_for(packet_number_space::application);
        if (application.has_value() && (!selected.has_value() || application->deadline < selected->deadline)) {
            selected = application;
        }
        return selected;
    }

    [[nodiscard]] std::optional<connection_lifecycle_timer> next_lifecycle_timer(std::chrono::steady_clock::time_point now) {
        if (state_ == connection_loop_state::closed) {
            return std::nullopt;
        }
        if (state_ == connection_loop_state::active) {
            if (config_.max_idle_timeout <= std::chrono::milliseconds{0}) {
                return std::nullopt;
            }
            if (!last_activity_at_.has_value()) {
                last_activity_at_ = now;
            }
            return connection_lifecycle_timer{connection_lifecycle_timer_kind::idle, *last_activity_at_ + config_.max_idle_timeout};
        }

        if (!close_state_entered_at_.has_value()) {
            close_state_entered_at_ = now;
        }
        const auto kind = state_ == connection_loop_state::closing ?
            connection_lifecycle_timer_kind::closing :
            connection_lifecycle_timer_kind::draining;
        return connection_lifecycle_timer{kind, *close_state_entered_at_ + config_.closing_draining_timeout};
    }

    void on_lifecycle_timer(connection_lifecycle_timer_kind kind, std::chrono::steady_clock::time_point now) {
        auto timer = next_lifecycle_timer(now);
        if (!timer.has_value() || timer->kind != kind || now < timer->deadline) {
            return;
        }

        if (kind == connection_lifecycle_timer_kind::idle && active()) {
            enter_closing(flowq::error{flowq::error_code::timeout, "idle timeout expired"}, now);
            return;
        }
        if ((kind == connection_lifecycle_timer_kind::closing && state_ == connection_loop_state::closing) ||
            (kind == connection_lifecycle_timer_kind::draining && state_ == connection_loop_state::draining)) {
            enter_closed();
        }
    }

    [[nodiscard]] connection_recovery_result on_recovery_timer(packet_number_space space, std::chrono::steady_clock::time_point now) {
        connection_recovery_result result{space, {}, {}};
        if (!active()) {
            return result;
        }
        if (!detail::is_connection_loop_space(space)) {
            return result;
        }
        refresh_key_lifecycle();
        if (!active()) {
            return result;
        }
        if (packet_space_discarded(space)) {
            clear_packet_space(space);
            return result;
        }

        const auto largest = largest_acknowledged(space);
        if (largest.has_value()) {
            auto detected = detect_time_threshold_losses(recovery_packets_, recovery_rtt_, space, *largest, now);
            result.newly_lost = std::move(detected.newly_lost);
            for (const auto packet_number : result.newly_lost) {
                sent_tracker(space).mark_lost(packet_number);
                congestion_.on_packet_lost(1200);
            }
            if (!result.newly_lost.empty()) {
                congestion_.on_congestion_event();
            }
            apply_stream_loss_mapping(space, result.newly_lost);
        }

        if (auto timer = recovery_timer_for(space)) {
            if (timer->mode == loss_timer_mode::pto && now >= timer->deadline && result.newly_lost.empty()) {
                result.newly_lost = mark_oldest_ack_eliciting_packet_lost(space);
                if (!result.newly_lost.empty()) {
                    congestion_.on_packet_lost(1200);
                    congestion_.on_congestion_event();
                    apply_stream_loss_mapping(space, result.newly_lost);
                }
            }
            result.next_deadline = timer->deadline;
        }
        return result;
    }

    void discard_packet_space(packet_number_space space) {
        if (!detail::is_connection_loop_space(space)) {
            return;
        }
        config_.key_lifecycle.discard(space);
        clear_packet_space(space);
    }

    void on_datagram(inbound_datagram datagram) {
        on_datagram(std::move(datagram), std::chrono::steady_clock::now());
    }

    void on_datagram(inbound_datagram datagram, std::chrono::steady_clock::time_point received_at) {
        if (!active()) {
            return;
        }
        refresh_key_lifecycle();
        if (!active()) {
            return;
        }
        if (!datagram.payload.empty() && (static_cast<std::uint8_t>(datagram.payload.data()[0]) & 0x80U) == 0) {
            if (packet_space_discarded(packet_number_space::application)) {
                clear_packet_space(packet_number_space::application);
                return;
            }
            auto parsed = parse_short_packet(
                datagram.payload,
                config_.local_connection_id.bytes.size(),
                detail::rx_protector_for(packet_number_space::application, config_));
            if (!parsed.ok()) {
                enter_closing(parsed.error, received_at);
                return;
            }
            process_parsed_packet(std::move(parsed), std::move(datagram.peer), datagram.payload.size(), received_at);
            return;
        }

        auto remaining = std::span<const std::byte>{datagram.payload.data(), datagram.payload.size()};
        while (!remaining.empty()) {
            const auto packet_size = detail::long_packet_wire_size(remaining);
            if (!packet_size.ok()) {
                if (all_zero_padding(remaining)) {
                    break;
                }
                enter_closing(packet_size.error, received_at);
                return;
            }

            const auto packet_bytes = remaining.first(packet_size.value);
            const auto header = decode_packet_header(packet_bytes);
            if (!header.ok()) {
                enter_closing(header.error, received_at);
                return;
            }
            update_remote_connection_id_from_header(header.header);

            const auto space = packet_space_for(header.header);
            if (packet_space_discarded(space)) {
                clear_packet_space(space);
                remaining = remaining.subspan(packet_size.value);
                continue;
            }
            const auto* protector = detail::rx_protector_for(space, config_);
            auto parsed = parse_long_packet(packet_bytes, protector);
            if (!parsed.ok()) {
                enter_closing(parsed.error, received_at);
                return;
            }

            process_parsed_packet(std::move(parsed), datagram.peer, packet_size.value, received_at);
            if (!active()) {
                return;
            }
            if (!refresh_packet_protectors(received_at)) {
                return;
            }
            remaining = remaining.subspan(packet_size.value);
        }
    }

    void acknowledge(packet_number_space space) {
        acknowledge(space, std::chrono::steady_clock::now());
    }

    void acknowledge(packet_number_space space, std::chrono::steady_clock::time_point sent_at) {
        if (!active()) {
            return;
        }
        if (!detail::is_connection_loop_space(space)) {
            enter_closing(flowq::error{flowq::error_code::protocol_error, "unsupported packet number space for connection loop"}, sent_at);
            return;
        }
        refresh_key_lifecycle();
        if (!active()) {
            return;
        }
        if (packet_space_discarded(space)) {
            clear_packet_space(space);
            return;
        }

        const auto& tracker = received_tracker(space);
        if (tracker.empty()) {
            return;
        }

        std::vector<frame> frames;
        frames.emplace_back(tracker.to_ack_frame());
        auto packet_number_value = next_packet_number_for(space);
        const auto assembled = assemble_space(space, packet_number_value, frames);
        if (!assembled.ok()) {
            enter_closing(assembled.error, sent_at);
            return;
        }
        if (!anti_amplification_allows(assembled.datagram.size())) {
            return;
        }

        ++packet_number_value;
        set_next_packet_number(space, packet_number_value);

        sent_tracker(space).on_packet_sent(assembled.number.value, false);
        recovery_packets_.push_back(recovery_packet{space, assembled.number.value, sent_at, false, sent_packet_state::outstanding});
        record_peer_bytes_sent(assembled.datagram.size());
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

#ifdef FLOWQ_ENABLE_INSPECTION
    [[nodiscard]] const sent_packet_tracker& sent_packets(packet_number_space space) const {
        return packet_spaces_.get(space).sent;
    }

    [[nodiscard]] std::vector<sent_stream_range> sent_stream_ranges(packet_number_space space, std::uint64_t packet_number) const {
        return find_sent_stream_ranges(space, packet_number);
    }

    [[nodiscard]] const congestion_controller& congestion() const noexcept {
        return congestion_;
    }

    [[nodiscard]] const stream_receive_state* receive_stream(std::uint64_t stream_id) const noexcept {
        return receive_streams_.find(stream_id);
    }

    [[nodiscard]] const stream_send_state* send_stream(std::uint64_t stream_id) const noexcept {
        return send_streams_.find(stream_id);
    }
#endif // FLOWQ_ENABLE_INSPECTION

    [[nodiscard]] connection_loop_state state() const noexcept {
        return state_;
    }

private:
    connection_loop_config config_;
    connection_loop_state state_{connection_loop_state::active};
    connection_packet_spaces packet_spaces_{};
    std::vector<connection_loop_action> actions_{};
    stream_receive_set receive_streams_{};
    stream_send_set send_streams_{};
    std::uint64_t connection_send_max_data_{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t connection_data_sent_{};
    bool peer_address_validated_{};
    std::uint64_t peer_bytes_received_{};
    std::uint64_t peer_bytes_sent_{};
    rtt_estimator recovery_rtt_{};
    pto_config recovery_pto_config_{};
    congestion_controller congestion_{};
    std::vector<recovery_packet> recovery_packets_{};
    bool lifecycle_dirty_{true};
    std::optional<std::chrono::steady_clock::time_point> last_activity_at_{};
    std::optional<std::chrono::steady_clock::time_point> close_state_entered_at_{};
    std::optional<std::uint64_t> largest_initial_acknowledged_{};
    std::optional<std::uint64_t> largest_handshake_acknowledged_{};
    std::optional<std::uint64_t> largest_application_acknowledged_{};
    std::vector<sent_packet_stream_ranges> sent_stream_ranges_{};

    [[nodiscard]] bool active() const noexcept {
        return state_ == connection_loop_state::active;
    }

    void mark_activity(std::chrono::steady_clock::time_point at) noexcept {
        last_activity_at_ = at;
    }

    [[nodiscard]] static std::uint64_t clamp_size_to_u64(std::size_t value) noexcept {
        constexpr auto max = std::numeric_limits<std::uint64_t>::max();
        if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
            if (value > static_cast<std::size_t>(max)) {
                return max;
            }
        }
        return static_cast<std::uint64_t>(value);
    }

    [[nodiscard]] static stream_initiator local_stream_initiator(connection_role role) noexcept {
        return role == connection_role::client ? stream_initiator::client : stream_initiator::server;
    }

    static void saturating_add(std::uint64_t& target, std::size_t value) noexcept {
        const auto increment = clamp_size_to_u64(value);
        const auto remaining = std::numeric_limits<std::uint64_t>::max() - target;
        target = increment > remaining ? std::numeric_limits<std::uint64_t>::max() : target + increment;
    }

    void enter_closing(flowq::error error) {
        enter_closing(std::move(error), std::chrono::steady_clock::now());
    }

    void enter_closing(flowq::error error, std::chrono::steady_clock::time_point at) {
        if (!active()) {
            return;
        }
        state_ = connection_loop_state::closing;
        close_state_entered_at_ = at;
        clear_all_packet_spaces();
        actions_.emplace_back(close_action{std::move(error)});
    }

    void enter_draining(flowq::error error) {
        enter_draining(std::move(error), std::chrono::steady_clock::now());
    }

    void enter_draining(flowq::error error, std::chrono::steady_clock::time_point at) {
        if (state_ == connection_loop_state::draining || state_ == connection_loop_state::closed) {
            return;
        }
        state_ = connection_loop_state::draining;
        close_state_entered_at_ = at;
        clear_all_packet_spaces();
        actions_.emplace_back(close_action{std::move(error)});
    }

    void enter_closed() {
        state_ = connection_loop_state::closed;
        close_state_entered_at_.reset();
        clear_all_packet_spaces();
        actions_.clear();
    }

    [[nodiscard]] static std::optional<flowq::error> peer_close_error(const std::vector<frame>& frames) {
        for (const auto& item : frames) {
            if (const auto* close = std::get_if<connection_close_frame>(&item)) {
                auto message = close->reason.empty() ? std::string{"peer closed connection"} : close->reason;
                return flowq::error{flowq::error_code::connection_closed, std::move(message)};
            }
            if (const auto* close = std::get_if<application_close_frame>(&item)) {
                auto message = close->reason.empty() ? std::string{"peer closed application"} : close->reason;
                return flowq::error{flowq::error_code::connection_closed, std::move(message)};
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] static const char* application_packet_space_only_frame_name(const frame& item) noexcept {
        if (std::holds_alternative<application_close_frame>(item)) {
            return "APPLICATION_CLOSE";
        }
        if (std::holds_alternative<reset_stream_frame>(item)) {
            return "RESET_STREAM";
        }
        if (std::holds_alternative<stop_sending_frame>(item)) {
            return "STOP_SENDING";
        }
        if (std::holds_alternative<new_token_frame>(item)) {
            return "NEW_TOKEN";
        }
        if (std::holds_alternative<stream_frame>(item)) {
            return "STREAM";
        }
        if (std::holds_alternative<max_data_frame>(item)) {
            return "MAX_DATA";
        }
        if (std::holds_alternative<max_stream_data_frame>(item)) {
            return "MAX_STREAM_DATA";
        }
        if (std::holds_alternative<data_blocked_frame>(item)) {
            return "DATA_BLOCKED";
        }
        if (std::holds_alternative<stream_data_blocked_frame>(item)) {
            return "STREAM_DATA_BLOCKED";
        }
        if (std::holds_alternative<max_streams_frame>(item)) {
            return "MAX_STREAMS";
        }
        if (std::holds_alternative<streams_blocked_frame>(item)) {
            return "STREAMS_BLOCKED";
        }
        if (std::holds_alternative<new_connection_id_frame>(item)) {
            return "NEW_CONNECTION_ID";
        }
        if (std::holds_alternative<retire_connection_id_frame>(item)) {
            return "RETIRE_CONNECTION_ID";
        }
        if (std::holds_alternative<handshake_done_frame>(item)) {
            return "HANDSHAKE_DONE";
        }
        if (std::holds_alternative<path_challenge_frame>(item)) {
            return "PATH_CHALLENGE";
        }
        if (std::holds_alternative<path_response_frame>(item)) {
            return "PATH_RESPONSE";
        }
        return nullptr;
    }

    [[nodiscard]] static flowq::error validate_inbound_frame_packet_space(packet_number_space space, const std::vector<frame>& frames) {
        if (space == packet_number_space::application) {
            return {};
        }
        for (const auto& item : frames) {
            if (const auto* name = application_packet_space_only_frame_name(item)) {
                return flowq::error{
                    flowq::error_code::protocol_error,
                    std::string{name} + " is only valid in Application packet space"};
            }
        }
        return {};
    }

    [[nodiscard]] static flowq::error validate_inbound_frame_role(connection_role local_role, const std::vector<frame>& frames) {
        if (local_role != connection_role::server) {
            return {};
        }
        for (const auto& item : frames) {
            if (std::holds_alternative<handshake_done_frame>(item)) {
                return flowq::error{
                    flowq::error_code::protocol_error,
                    "HANDSHAKE_DONE frames are only sent by servers"};
            }
            if (std::holds_alternative<new_token_frame>(item)) {
                return flowq::error{
                    flowq::error_code::protocol_error,
                    "NEW_TOKEN frames are only sent by servers"};
            }
        }
        return {};
    }

    [[nodiscard]] static bool ack_ranges_valid(const ack_frame& ack) noexcept {
        if (ack.first_ack_range > ack.largest_acknowledged) {
            return false;
        }

        auto range_low = ack.largest_acknowledged - ack.first_ack_range;
        for (const auto& range : ack.ranges) {
            if (range.gap > std::numeric_limits<std::uint64_t>::max() - 2U) {
                return false;
            }
            const auto skipped_packets = range.gap + 2U;
            if (range_low < skipped_packets) {
                return false;
            }

            const auto range_high = range_low - skipped_packets;
            if (range.length > range_high) {
                return false;
            }
            range_low = range_high - range.length;
        }

        return true;
    }

    [[nodiscard]] static flowq::error validate_inbound_ack_ranges(const std::vector<frame>& frames) {
        for (const auto& item : frames) {
            if (const auto* ack = std::get_if<ack_frame>(&item); ack != nullptr && !ack_ranges_valid(*ack)) {
                return flowq::error{
                    flowq::error_code::protocol_error,
                    "ACK frame contains invalid packet number ranges"};
            }
        }
        return {};
    }

    [[nodiscard]] static bool same_endpoint(const flowq::endpoint& lhs, const flowq::endpoint& rhs) noexcept {
        return lhs.host == rhs.host && lhs.port == rhs.port && lhs.alpn == rhs.alpn;
    }

    [[nodiscard]] static bool all_zero_padding(std::span<const std::byte> bytes) noexcept {
        return std::all_of(bytes.begin(), bytes.end(), [](std::byte item) {
            return item == std::byte{0x00};
        });
    }

    void update_remote_connection_id_from_header(const packet_header& header) {
        const connection_id* source = nullptr;
        if (const auto* initial = std::get_if<initial_header>(&header)) {
            source = &initial->source_connection_id;
        } else if (const auto* handshake = std::get_if<handshake_header>(&header)) {
            source = &handshake->source_connection_id;
        } else if (const auto* retry = std::get_if<retry_header>(&header)) {
            source = &retry->source_connection_id;
        }

        if (source == nullptr || source->bytes.empty()) {
            return;
        }
        config_.remote_connection_id = *source;
    }

    [[nodiscard]] bool refresh_packet_protectors(std::chrono::steady_clock::time_point at) {
        if (!config_.packet_protector_refresh) {
            return true;
        }
        auto update = config_.packet_protector_refresh();
        if (!update.ok()) {
            enter_closing(std::move(update.error), at);
            return false;
        }
        set_packet_protectors(
            update.handshake_tx,
            update.handshake_rx,
            update.application_tx,
            update.application_rx);
        return true;
    }

    [[nodiscard]] bool accept_peer(flowq::endpoint peer, std::chrono::steady_clock::time_point received_at) {
        if (same_endpoint(peer, config_.peer)) {
            return true;
        }
        if (config_.disable_active_migration) {
            enter_closing(flowq::error{flowq::error_code::protocol_error, "peer address changed while active migration is disabled"}, received_at);
            return false;
        }
        config_.peer = std::move(peer);
        reset_peer_path_validation();
        return true;
    }

    [[nodiscard]] static packet_number_space packet_space_for(const packet_header& header) noexcept {
        if (std::holds_alternative<short_header>(header)) {
            return packet_number_space::application;
        }
        return std::holds_alternative<handshake_header>(header) ? packet_number_space::handshake : packet_number_space::initial;
    }

    [[nodiscard]] received_packet_tracker& received_tracker(packet_number_space space) noexcept {
        return packet_spaces_.get(space).received;
    }

    [[nodiscard]] const received_packet_tracker& received_tracker(packet_number_space space) const noexcept {
        return packet_spaces_.get(space).received;
    }

    [[nodiscard]] sent_packet_tracker& sent_tracker(packet_number_space space) noexcept {
        return packet_spaces_.get(space).sent;
    }

    [[nodiscard]] assembled_packet assemble_space(packet_number_space space, std::uint64_t packet_number_value, const std::vector<frame>& frames) const {
        if (space == packet_number_space::application) {
            return assemble_application_packet(application_packet_build_request{
                config_.remote_connection_id,
                packet_number{space, packet_number_value},
                frames,
                detail::tx_protector_for(space, config_),
                config_.pipeline
            });
        }
        return assemble_long_packet(packet_build_request{
            detail::long_type_for(space),
            config_.version,
            config_.remote_connection_id,
            config_.local_connection_id,
            {},
            packet_number{space, packet_number_value},
            frames,
            detail::tx_protector_for(space, config_),
            config_.pipeline
        });
    }

    [[nodiscard]] std::uint64_t next_packet_number_for(packet_number_space space) const noexcept {
        return packet_spaces_.get(space).next_packet_number;
    }

    void set_next_packet_number(packet_number_space space, std::uint64_t value) noexcept {
        packet_spaces_.get(space).next_packet_number = value;
    }

    [[nodiscard]] bool application_send_allowed() const noexcept {
        return config_.tls_adapter != nullptr && application_data_ready(*config_.tls_adapter) &&
               config_.key_lifecycle.available(encryption_level::one_rtt, key_direction::send);
    }

    [[nodiscard]] bool anti_amplification_limited() const noexcept {
        return config_.role == connection_role::server && !peer_address_validated_;
    }

    [[nodiscard]] bool anti_amplification_allows(std::size_t datagram_size) const noexcept {
        if (!anti_amplification_limited()) {
            return true;
        }
        const auto outbound_size = clamp_size_to_u64(datagram_size);
        if (peer_bytes_received_ > std::numeric_limits<std::uint64_t>::max() / 3U) {
            return true;
        }
        const auto allowance = peer_bytes_received_ * 3U;
        return allowance >= peer_bytes_sent_ && outbound_size <= allowance - peer_bytes_sent_;
    }

    void refresh_peer_address_validation() noexcept {
        if (!anti_amplification_limited() || config_.tls_adapter == nullptr) {
            return;
        }
        if (config_.tls_adapter->state() == handshake_state::handshake_confirmed) {
            peer_address_validated_ = true;
        }
    }

    void reset_peer_path_validation() noexcept {
        if (config_.role != connection_role::server) {
            return;
        }
        peer_address_validated_ = false;
        peer_bytes_received_ = 0;
        peer_bytes_sent_ = 0;
    }

    void record_peer_bytes_received(std::size_t datagram_size) noexcept {
        if (anti_amplification_limited()) {
            saturating_add(peer_bytes_received_, datagram_size);
        }
    }

    void record_peer_bytes_sent(std::size_t datagram_size) noexcept {
        if (anti_amplification_limited()) {
            saturating_add(peer_bytes_sent_, datagram_size);
        }
    }

    void refresh_key_lifecycle() {
        if (lifecycle_dirty_) {
            if (config_.tls_adapter != nullptr) {
                const auto tls_state = config_.tls_adapter->state();
                config_.key_lifecycle.observe_tls(tls_state, config_.tls_adapter->key_availability());
                recovery_pto_config_.handshake_confirmed = tls_state == handshake_state::handshake_confirmed;
            }
            clear_discarded_packet_spaces();
            lifecycle_dirty_ = false;
        }
        refresh_peer_address_validation();
    }

    void mark_lifecycle_dirty() noexcept {
        lifecycle_dirty_ = true;
    }

    [[nodiscard]] bool packet_space_discarded(packet_number_space space) const noexcept {
        return config_.key_lifecycle.discarded(space);
    }

    void pump_tls_crypto() {
        if (config_.tls_adapter == nullptr) {
            return;
        }
        auto error = config_.tls_adapter->advance();
        if (!error.ok()) {
            enter_closing(error, std::chrono::steady_clock::now());
            return;
        }
        auto outbound = config_.tls_adapter->drain_crypto();
        mark_lifecycle_dirty();
        for (auto& item : outbound) {
            const auto space = detail::packet_space_for(item.level);
            if (packet_space_discarded(space)) {
                continue;
            }
            auto frame = flowq::quic::frame{crypto_frame{item.offset, std::move(item.data)}};
            switch (space) {
            case packet_number_space::initial:
                packet_spaces_.get(packet_number_space::initial).queue.push_back(std::move(frame));
                break;
            case packet_number_space::handshake:
                packet_spaces_.get(packet_number_space::handshake).queue.push_back(std::move(frame));
                break;
            case packet_number_space::application:
                packet_spaces_.get(packet_number_space::application).queue.push_back(std::move(frame));
                break;
            }
        }
    }

    void flush_space(packet_number_space space, std::chrono::steady_clock::time_point sent_at) {
        auto& state = packet_spaces_.get(space);
        auto& queue = state.queue;
        if (queue.empty()) {
            return;
        }
        if (packet_space_discarded(space)) {
            clear_packet_space(space);
            return;
        }
        if (!congestion_.can_send()) {
            return;
        }
        if (space == packet_number_space::application && !application_send_allowed()) {
            enter_closing(flowq::error{flowq::error_code::protocol_error, "production Application data requires confirmed TLS handshake and application keys"}, sent_at);
            return;
        }

        auto selected = select_frames_for_payload_budget(queue, config_.max_packet_payload_size);
        if (!selected.ok()) {
            enter_closing(selected.error, sent_at);
            return;
        }
        if (selected.frames.empty()) {
            enter_closing(flowq::error{flowq::error_code::protocol_error, "frame exceeds packet payload budget"}, sent_at);
            return;
        }

        const auto ack_eliciting = detail::is_ack_eliciting(selected.frames);
        auto assembled = assemble_space(space, state.next_packet_number, selected.frames);
        if (!assembled.ok()) {
            enter_closing(assembled.error, sent_at);
            return;
        }
        if (!anti_amplification_allows(assembled.datagram.size())) {
            return;
        }

        ++state.next_packet_number;
        sent_tracker(space).on_packet_sent(assembled.number.value, ack_eliciting);
        recovery_packets_.push_back(recovery_packet{space, assembled.number.value, sent_at, ack_eliciting, sent_packet_state::outstanding});
        record_sent_stream_ranges(space, assembled.number.value, selected.frames);
        congestion_.on_packet_sent(assembled.datagram.size(), ack_eliciting);
        record_peer_bytes_sent(assembled.datagram.size());
        queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(selected.next_index));
        mark_activity(sent_at);
        actions_.emplace_back(outbound_datagram{std::move(assembled.datagram), config_.peer});
    }

    void apply_ack_frames(packet_number_space space, const std::vector<frame>& frames) {
        for (const auto& item : frames) {
            if (const auto* ack = std::get_if<ack_frame>(&item)) {
                record_largest_acknowledged(space, ack->largest_acknowledged);
                auto result = sent_tracker(space).on_ack_received(*ack);
                mark_recovery_packets(space, result.newly_acknowledged, sent_packet_state::acknowledged);
                mark_recovery_packets(space, result.newly_lost, sent_packet_state::lost);
                apply_stream_ack_mapping(space, result.newly_acknowledged);
                apply_stream_loss_mapping(space, result.newly_lost);
                if (!result.newly_acknowledged.empty()) {
                    congestion_.on_packet_acknowledged(1200);
                }
                if (!result.newly_lost.empty()) {
                    congestion_.on_congestion_event();
                }
            }
        }
    }

    void record_sent_stream_ranges(packet_number_space space, std::uint64_t packet_number, const std::vector<frame>& frames) {
        sent_packet_stream_ranges packet{space, packet_number, {}};
        for (const auto& item : frames) {
            if (const auto* stream = std::get_if<stream_frame>(&item)) {
                packet.ranges.push_back(sent_stream_range{
                    stream->stream_id,
                    stream_send_range{stream->offset_present ? stream->offset : 0, static_cast<std::uint64_t>(stream->data.size()), stream->fin}
                });
            }
        }
        if (!packet.ranges.empty()) {
            sent_stream_ranges_.push_back(std::move(packet));
        }
    }

    [[nodiscard]] std::vector<sent_stream_range> find_sent_stream_ranges(packet_number_space space, std::uint64_t packet_number) const {
        for (const auto& packet : sent_stream_ranges_) {
            if (packet.space == space && packet.packet_number == packet_number) {
                return packet.ranges;
            }
        }
        return {};
    }

    void apply_stream_ack_mapping(packet_number_space space, const std::vector<std::uint64_t>& packet_numbers) {
        for (const auto packet_number : packet_numbers) {
            for (const auto& range : find_sent_stream_ranges(space, packet_number)) {
                send_streams_.on_acked(range.stream_id, range.range);
            }
        }
    }

    void apply_stream_loss_mapping(packet_number_space space, const std::vector<std::uint64_t>& packet_numbers) {
        for (const auto packet_number : packet_numbers) {
            for (const auto& range : find_sent_stream_ranges(space, packet_number)) {
                send_streams_.on_lost(range.stream_id, range.range);
            }
        }
    }

    [[nodiscard]] std::vector<std::uint64_t> mark_oldest_ack_eliciting_packet_lost(packet_number_space space) {
        for (auto& packet : recovery_packets_) {
            if (packet.space == space && packet.state == sent_packet_state::outstanding && packet.ack_eliciting) {
                packet.state = sent_packet_state::lost;
                sent_tracker(space).mark_lost(packet.packet_number);
                return {packet.packet_number};
            }
        }
        return {};
    }

    [[nodiscard]] std::optional<connection_recovery_timer> recovery_timer_for(packet_number_space space) const {
        if (packet_space_discarded(space)) {
            return std::nullopt;
        }
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
        auto& stored = packet_spaces_.get(space).largest_acknowledged;
        if (!stored.has_value() || packet_number > *stored) {
            stored = packet_number;
        }
    }

    [[nodiscard]] std::optional<std::uint64_t> largest_acknowledged(packet_number_space space) const noexcept {
        return packet_spaces_.get(space).largest_acknowledged;
    }

    void clear_discarded_packet_spaces() {
        if (packet_space_discarded(packet_number_space::initial)) {
            clear_packet_space(packet_number_space::initial);
        }
        if (packet_space_discarded(packet_number_space::handshake)) {
            clear_packet_space(packet_number_space::handshake);
        }
        if (packet_space_discarded(packet_number_space::application)) {
            clear_packet_space(packet_number_space::application);
        }
    }

    void clear_all_packet_spaces() {
        clear_packet_space(packet_number_space::initial);
        clear_packet_space(packet_number_space::handshake);
        clear_packet_space(packet_number_space::application);
    }

    void clear_packet_space(packet_number_space space) {
        queue_for(space).clear();
        received_tracker(space).clear();
        sent_tracker(space).clear();
        largest_acknowledged_ref(space).reset();
        recovery_packets_.erase(
            std::remove_if(
                recovery_packets_.begin(),
                recovery_packets_.end(),
                [space](const recovery_packet& packet) {
                    return packet.space == space;
                }),
            recovery_packets_.end());
        sent_stream_ranges_.erase(
            std::remove_if(
                sent_stream_ranges_.begin(),
                sent_stream_ranges_.end(),
                [space](const sent_packet_stream_ranges& packet) {
                    return packet.space == space;
                }),
            sent_stream_ranges_.end());
    }

    [[nodiscard]] std::vector<frame>& queue_for(packet_number_space space) noexcept {
        return packet_spaces_.get(space).queue;
    }

    [[nodiscard]] std::optional<std::uint64_t>& largest_acknowledged_ref(packet_number_space space) noexcept {
        return packet_spaces_.get(space).largest_acknowledged;
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
            if (const auto* stream_data_credit = std::get_if<max_stream_data_frame>(&item)) {
                send_streams_.update_max_data(*stream_data_credit);
            } else if (const auto* connection_data_credit = std::get_if<max_data_frame>(&item)) {
                update_max_data(*connection_data_credit);
            } else if (const auto* stream_credit = std::get_if<max_streams_frame>(&item)) {
                send_streams_.update_max_streams(*stream_credit);
            }
        }
    }

    [[nodiscard]] flowq::error apply_stream_control_frames(const std::vector<frame>& frames) {
        for (const auto& item : frames) {
            if (const auto* reset = std::get_if<reset_stream_frame>(&item)) {
                auto delivery = receive_streams_.reset(*reset);
                if (!delivery.result.ok()) {
                    return delivery.result.error;
                }
            } else if (const auto* stop = std::get_if<stop_sending_frame>(&item)) {
                auto result = send_streams_.stop_sending(*stop);
                if (!result.ok()) {
                    return result.error;
                }
            }
        }
        return {};
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

    [[nodiscard]] flowq::error receive_crypto_frames(packet_number_space space, const std::vector<frame>& frames) {
        if (config_.tls_adapter == nullptr) {
            return {};
        }
        for (const auto& item : frames) {
            if (const auto* crypto = std::get_if<crypto_frame>(&item)) {
                auto error = config_.tls_adapter->receive_crypto(crypto_bytes{detail::tls_level_for(space), crypto->offset, crypto->data});
                if (!error.ok()) {
                    return error;
                }
                mark_lifecycle_dirty();
            }
        }
        return {};
    }

    void apply_path_validation_frames(const std::vector<frame>& frames) {
        for (const auto& item : frames) {
            if (const auto* challenge = std::get_if<path_challenge_frame>(&item)) {
                packet_spaces_.get(packet_number_space::application).queue.push_back(frame{path_response_frame{challenge->data}});
            }
        }
    }

    void process_parsed_packet(parsed_packet parsed, flowq::endpoint peer, std::size_t received_size, std::chrono::steady_clock::time_point received_at) {
        const auto newly_observed = received_tracker(parsed.number.space).observe(parsed.number.value);
        if (!newly_observed) {
            return;
        }
        if (!accept_peer(std::move(peer), received_at)) {
            return;
        }
        record_peer_bytes_received(received_size);
        mark_activity(received_at);
        if (auto error = validate_inbound_frame_packet_space(parsed.number.space, parsed.frames); !error.ok()) {
            enter_closing(error, received_at);
            return;
        }
        if (auto error = validate_inbound_frame_role(config_.role, parsed.frames); !error.ok()) {
            enter_closing(error, received_at);
            return;
        }
        if (auto error = validate_inbound_ack_ranges(parsed.frames); !error.ok()) {
            enter_closing(error, received_at);
            return;
        }
        if (auto error = peer_close_error(parsed.frames); error.has_value()) {
            enter_draining(std::move(*error), received_at);
            return;
        }
        apply_path_validation_frames(parsed.frames);
        apply_ack_frames(parsed.number.space, parsed.frames);
        apply_flow_control_frames(parsed.frames);
        if (auto error = apply_stream_control_frames(parsed.frames); !error.ok()) {
            enter_closing(error, received_at);
            return;
        }
        if (auto error = receive_crypto_frames(parsed.number.space, parsed.frames); !error.ok()) {
            enter_closing(error, received_at);
            return;
        }
        refresh_key_lifecycle();
        auto stream_deliveries = receive_stream_frames(parsed.frames);
        for (const auto& delivery : stream_deliveries) {
            if (!delivery.result.ok()) {
                enter_closing(delivery.result.error, received_at);
                return;
            }
        }
        actions_.emplace_back(received_packet_event{parsed.number, std::move(parsed.frames), config_.peer, std::move(stream_deliveries)});
    }
};

} // namespace flowq::quic
