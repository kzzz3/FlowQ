#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>
#include <flowq/quic/frame.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace flowq::quic {

enum class stream_initiator {
    client,
    server
};

struct stream_id_info {
    std::uint64_t id{};
    stream_initiator initiator{};
    stream_direction direction{};
};

[[nodiscard]] inline stream_id_info classify_stream_id(std::uint64_t id) noexcept {
    return {
        id,
        (id & 0x01U) == 0 ? stream_initiator::client : stream_initiator::server,
        (id & 0x02U) == 0 ? stream_direction::bidirectional : stream_direction::unidirectional
    };
}

struct stream_limits {
    std::uint64_t bidirectional{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t unidirectional{std::numeric_limits<std::uint64_t>::max()};
};

struct stream_receive_result {
    flowq::buffer data;
    bool final_size_known{};
    std::uint64_t final_size{};
    bool closed{};
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

namespace detail {

[[nodiscard]] inline flowq::error stream_error(const char* message) {
    return flowq::error{flowq::error_code::protocol_error, message};
}

[[nodiscard]] inline flowq::error stream_flow_control_error(const char* message) {
    return flowq::error{flowq::error_code::flow_control_error, message};
}

inline void append_stream_buffer(std::vector<std::byte>& output, const flowq::buffer& input) {
    output.insert(output.end(), input.data(), input.data() + static_cast<std::ptrdiff_t>(input.size()));
}

} // namespace detail

class stream_receive_state {
public:
    explicit stream_receive_state(std::uint64_t max_data = std::numeric_limits<std::uint64_t>::max()) noexcept : max_data_{max_data} {}

    stream_receive_state(stream_receive_state&&) noexcept = default;
    stream_receive_state& operator=(stream_receive_state&&) noexcept = default;

    [[nodiscard]] stream_receive_result receive(const stream_frame& frame) {
        if (reset_received_) {
            return failure("STREAM data received after RESET_STREAM");
        }

        const auto offset = frame.offset_present ? frame.offset : 0;
        const auto frame_size = static_cast<std::uint64_t>(frame.data.size());
        if (offset > std::numeric_limits<std::uint64_t>::max() - frame_size) {
            return failure("STREAM frame offset overflows");
        }
        const auto end = offset + frame_size;

        if (end > max_data_) {
            return flow_control_failure("STREAM data exceeds stream flow-control credit");
        }

        std::optional<std::uint64_t> proposed_final_size = final_size_;
        if (frame.fin) {
            if (final_size_.has_value() && *final_size_ != end) {
                return failure("STREAM final size changed");
            }
            proposed_final_size = end;
        }

        if (proposed_final_size.has_value() && end > *proposed_final_size) {
            return failure("STREAM data exceeds final size");
        }

        const auto validation_error = validate_overlap(offset, frame.data);
        if (!validation_error.ok()) {
            return {{}, final_size_.has_value(), final_size_value(), closed(), validation_error};
        }

        final_size_ = proposed_final_size;
        buffer_new_suffix(offset, frame.data);
        return success(drain_contiguous());
    }

    [[nodiscard]] stream_receive_result reset(const reset_stream_frame& frame) {
        if (final_size_.has_value() && *final_size_ != frame.final_size) {
            return failure("RESET_STREAM final size conflicts with known final size");
        }
        if (frame.final_size < largest_received_offset()) {
            return failure("RESET_STREAM final size is below delivered offset");
        }

        final_size_ = frame.final_size;
        reset_received_ = true;
        reset_error_code_ = frame.application_error_code;
        return {{}, true, frame.final_size, true, {}};
    }

    [[nodiscard]] std::uint64_t next_offset() const noexcept {
        return next_offset_;
    }

    [[nodiscard]] bool final_size_known() const noexcept {
        return final_size_.has_value();
    }

    [[nodiscard]] std::uint64_t final_size() const noexcept {
        return final_size_value();
    }

    [[nodiscard]] bool closed() const noexcept {
        return reset_received_ || (final_size_.has_value() && next_offset_ == *final_size_);
    }

    [[nodiscard]] bool reset_received() const noexcept {
        return reset_received_;
    }

    [[nodiscard]] std::uint64_t reset_error_code() const noexcept {
        return reset_error_code_.value_or(0);
    }

    [[nodiscard]] std::uint64_t reset_final_size() const noexcept {
        return final_size_value();
    }

    void update_max_data(std::uint64_t max_data) noexcept {
        max_data_ = std::max(max_data_, max_data);
    }

    [[nodiscard]] std::uint64_t max_data() const noexcept {
        return max_data_;
    }

private:
    std::uint64_t next_offset_{};
    std::uint64_t max_data_{std::numeric_limits<std::uint64_t>::max()};
    std::vector<std::byte> delivered_{};
    std::map<std::uint64_t, flowq::buffer> pending_{};
    std::optional<std::uint64_t> final_size_{};
    bool reset_received_{};
    std::optional<std::uint64_t> reset_error_code_{};

    [[nodiscard]] std::uint64_t final_size_value() const noexcept {
        return final_size_.value_or(0);
    }

    [[nodiscard]] std::uint64_t largest_received_offset() const noexcept {
        auto largest = next_offset_;
        for (const auto& [offset, data] : pending_) {
            largest = std::max(largest, offset + static_cast<std::uint64_t>(data.size()));
        }
        return largest;
    }

    [[nodiscard]] stream_receive_result success(flowq::buffer data) const {
        return {std::move(data), final_size_.has_value(), final_size_value(), closed(), {}};
    }

    [[nodiscard]] stream_receive_result failure(const char* message) const {
        return {{}, final_size_.has_value(), final_size_value(), closed(), detail::stream_error(message)};
    }

    [[nodiscard]] stream_receive_result flow_control_failure(const char* message) const {
        return {{}, final_size_.has_value(), final_size_value(), closed(), detail::stream_flow_control_error(message)};
    }

    [[nodiscard]] flowq::error validate_overlap(std::uint64_t offset, const flowq::buffer& data) const {
        for (std::size_t index = 0; index < data.size(); ++index) {
            const auto absolute = offset + static_cast<std::uint64_t>(index);
            const auto expected = byte_at(absolute);
            if (expected.has_value() && *expected != data.data()[index]) {
                return detail::stream_error("STREAM data conflicts with existing bytes");
            }
        }
        return {};
    }

    [[nodiscard]] std::optional<std::byte> byte_at(std::uint64_t offset) const {
        if (offset < delivered_.size()) {
            return delivered_[static_cast<std::size_t>(offset)];
        }

        const auto after = pending_.upper_bound(offset);
        if (after == pending_.begin()) {
            return std::nullopt;
        }

        const auto candidate = std::prev(after);
        const auto start = candidate->first;
        const auto& bytes = candidate->second;
        const auto end = start + static_cast<std::uint64_t>(bytes.size());
        if (offset >= start && offset < end) {
            return bytes.data()[static_cast<std::size_t>(offset - start)];
        }
        return std::nullopt;
    }

    void buffer_new_suffix(std::uint64_t offset, const flowq::buffer& data) {
        if (data.empty()) {
            return;
        }

        const auto data_end = offset + static_cast<std::uint64_t>(data.size());
        auto merged_start = std::max(offset, next_offset_);
        if (merged_start >= data_end) {
            return;
        }

        auto merged_end = data_end;
        auto first = pending_.lower_bound(merged_start);
        if (first != pending_.begin()) {
            auto previous = std::prev(first);
            const auto previous_end = previous->first + static_cast<std::uint64_t>(previous->second.size());
            if (previous_end >= merged_start) {
                first = previous;
            }
        }

        auto last = first;
        while (last != pending_.end()) {
            const auto range_start = last->first;
            const auto range_end = range_start + static_cast<std::uint64_t>(last->second.size());
            if (range_start > merged_end) {
                break;
            }
            merged_start = std::min(merged_start, range_start);
            merged_end = std::max(merged_end, range_end);
            ++last;
        }

        std::vector<std::byte> merged(static_cast<std::size_t>(merged_end - merged_start));
        auto write_byte = [&](std::uint64_t absolute, std::byte value) {
            merged[static_cast<std::size_t>(absolute - merged_start)] = value;
        };

        for (auto iterator = first; iterator != last; ++iterator) {
            const auto range_start = iterator->first;
            const auto& bytes = iterator->second;
            for (std::size_t index = 0; index < bytes.size(); ++index) {
                write_byte(range_start + static_cast<std::uint64_t>(index), bytes.data()[index]);
            }
        }

        for (auto absolute = std::max(offset, merged_start); absolute < data_end; ++absolute) {
            write_byte(absolute, data.data()[static_cast<std::size_t>(absolute - offset)]);
        }

        if (first != last) {
            pending_.erase(first, last);
        }

        if (merged.empty()) {
            return;
        }
        pending_.emplace(merged_start, flowq::buffer{merged});
    }

    [[nodiscard]] flowq::buffer drain_contiguous() {
        std::vector<std::byte> output;
        while (true) {
            auto current = pending_.find(next_offset_);
            if (current == pending_.end()) {
                break;
            }

            detail::append_stream_buffer(output, current->second);
            detail::append_stream_buffer(delivered_, current->second);
            next_offset_ += current->second.size();
            pending_.erase(current);
        }
        return flowq::buffer{output};
    }
};

struct stream_delivery {
    std::uint64_t stream_id{};
    stream_receive_result result;
};

class stream_receive_set {
public:
    explicit stream_receive_set(
        std::uint64_t initial_max_data = std::numeric_limits<std::uint64_t>::max(),
        stream_initiator local_initiator = stream_initiator::client,
        stream_limits peer_limits = {}) noexcept
        : initial_max_data_{initial_max_data}, local_initiator_{local_initiator}, peer_limits_{peer_limits} {}

    [[nodiscard]] stream_delivery receive(const stream_frame& frame) {
        auto* state = ensure_state_for(frame.stream_id);
        if (state == nullptr) {
            return {frame.stream_id, stream_receive_result{{}, false, 0, false, detail::stream_error("peer stream count limit exceeded")}};
        }
        return {frame.stream_id, state->receive(frame)};
    }

    [[nodiscard]] stream_delivery reset(const reset_stream_frame& frame) {
        auto& state = state_for(frame.stream_id);
        return {frame.stream_id, state.reset(frame)};
    }

    void update_max_data(std::uint64_t stream_id, std::uint64_t max_data) {
        state_for(stream_id).update_max_data(max_data);
    }

    void update_max_data(const max_stream_data_frame& frame) {
        update_max_data(frame.stream_id, frame.maximum_stream_data);
    }

    [[nodiscard]] const stream_receive_state* find(std::uint64_t stream_id) const noexcept {
        const auto found = streams_.find(stream_id);
        return found == streams_.end() ? nullptr : &found->second;
    }

private:
    std::uint64_t initial_max_data_{std::numeric_limits<std::uint64_t>::max()};
    stream_initiator local_initiator_{stream_initiator::client};
    stream_limits peer_limits_{};
    std::map<std::uint64_t, stream_receive_state> streams_{};

    [[nodiscard]] stream_receive_state& state_for(std::uint64_t stream_id) {
        auto [iterator, inserted] = streams_.try_emplace(stream_id, initial_max_data_);
        (void)inserted;
        return iterator->second;
    }

    [[nodiscard]] stream_receive_state* find_mutable(std::uint64_t stream_id) noexcept {
        const auto found = streams_.find(stream_id);
        return found == streams_.end() ? nullptr : &found->second;
    }

    [[nodiscard]] stream_receive_state* ensure_state_for(std::uint64_t stream_id) {
        if (auto* existing = find_mutable(stream_id)) {
            return existing;
        }
        const auto info = classify_stream_id(stream_id);
        if (info.initiator != local_initiator_) {
            const auto ordinal = stream_id >> 2U;
            const auto limit = info.direction == stream_direction::bidirectional ? peer_limits_.bidirectional : peer_limits_.unidirectional;
            if (ordinal >= limit) {
                return nullptr;
            }
        }
        return &state_for(stream_id);
    }
};

struct stream_operation_result {
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

struct stream_send_range {
    std::uint64_t offset{};
    std::uint64_t length{};
    bool fin{};
};

struct stream_send_result {
    stream_frame frame{};
    stream_send_range range{};
    bool has_frame{};
    flowq::error error{};
    bool retransmission{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

struct stream_frame_schedule_result {
    std::vector<frame> frames;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

class stream_send_state {
public:
    explicit stream_send_state(std::uint64_t stream_id, std::uint64_t max_data = std::numeric_limits<std::uint64_t>::max())
        : stream_id_{stream_id}, max_data_{max_data} {}

    stream_send_state(stream_send_state&&) noexcept = default;
    stream_send_state& operator=(stream_send_state&&) noexcept = default;

    [[nodiscard]] stream_operation_result append(const flowq::buffer& data) {
        if (stop_requested_) {
            return {detail::stream_error("cannot append STREAM data after STOP_SENDING")};
        }
        if (finished_) {
            return {detail::stream_error("cannot append STREAM data after FIN")};
        }
        if (bytes_.size() > static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max()) - data.size()) {
            return {detail::stream_error("STREAM send offset overflows")};
        }

        detail::append_stream_buffer(bytes_, data);
        return {};
    }

    [[nodiscard]] stream_operation_result finish() noexcept {
        if (stop_requested_) {
            return {detail::stream_error("cannot finish STREAM after STOP_SENDING")};
        }
        finished_ = true;
        return {};
    }

    [[nodiscard]] stream_send_result pop_frame(std::size_t max_data_size) {
        if (stop_requested_) {
            return {};
        }

        if (!lost_.empty()) {
            auto range = lost_.front();
            lost_.erase(lost_.begin());
            if (is_acked(range)) {
                return pop_frame(max_data_size);
            }
            if (range.length > 0 && max_data_size == 0) {
                lost_.insert(lost_.begin(), range);
                return {};
            }
            if (range.length > max_data_size && max_data_size > 0) {
                const auto remaining = stream_send_range{range.offset + max_data_size, range.length - max_data_size, range.fin};
                range.length = max_data_size;
                range.fin = false;
                lost_.insert(lost_.begin(), remaining);
            }
            if (range.fin) {
                fin_sent_ = true;
            }
            auto result = make_frame(range);
            result.retransmission = true;
            return result;
        }

        if (next_unsent_offset_ < bytes_.size() && max_data_size > 0) {
            if (next_unsent_offset_ >= max_data_) {
                return {};
            }
            const auto available = bytes_.size() - static_cast<std::size_t>(next_unsent_offset_);
            const auto credit = static_cast<std::size_t>(max_data_ - next_unsent_offset_);
            const auto length = std::min(max_data_size, std::min(available, credit));
            const auto range = stream_send_range{
                next_unsent_offset_,
                static_cast<std::uint64_t>(length),
                finished_ && next_unsent_offset_ + length == bytes_.size() && bytes_.size() <= max_data_
            };
            next_unsent_offset_ += length;
            if (range.fin) {
                fin_sent_ = true;
            }
            return make_frame(range);
        }

        if (finished_ && !fin_sent_ && !fin_acked_ && next_unsent_offset_ == bytes_.size() && bytes_.size() <= max_data_) {
            fin_sent_ = true;
            return make_frame(stream_send_range{static_cast<std::uint64_t>(bytes_.size()), 0, true});
        }

        return {};
    }

    void on_acked(stream_send_range range) {
        if (!valid_sent_range(range)) {
            return;
        }
        if (range.fin && !fin_sent_ && !covers_lost_fin(range)) {
            range.fin = false;
        }
        if (range.fin && (!finished_ || range.offset + range.length != bytes_.size())) {
            range.fin = false;
        }
        acked_.push_back(range);
        lost_.erase(
            std::remove_if(
                lost_.begin(),
                lost_.end(),
                [range](const stream_send_range& lost) {
                    return stream_send_state::range_covers(range, lost);
                }),
            lost_.end());
        if (range.fin) {
            fin_acked_ = true;
        }
    }

    void on_lost(stream_send_range range) {
        if (stop_requested_) {
            return;
        }
        if (!valid_sent_range(range)) {
            return;
        }
        if (range.fin && !fin_sent_) {
            return;
        }
        if (is_acked(range)) {
            return;
        }
        if (range.fin) {
            fin_sent_ = false;
        }
        lost_.push_back(range);
    }

    void update_max_data(std::uint64_t max_data) noexcept {
        max_data_ = std::max(max_data_, max_data);
    }

    [[nodiscard]] stream_operation_result stop_sending(const stop_sending_frame& frame) {
        if (frame.stream_id != stream_id_) {
            return {detail::stream_error("STOP_SENDING stream id does not match send state")};
        }
        stop_requested_ = true;
        stop_error_code_ = frame.application_error_code;
        lost_.clear();
        return {};
    }

    void update_max_data(const max_stream_data_frame& frame) noexcept {
        if (frame.stream_id == stream_id_) {
            update_max_data(frame.maximum_stream_data);
        }
    }

    [[nodiscard]] std::uint64_t max_data() const noexcept {
        return max_data_;
    }

    [[nodiscard]] bool blocked() const noexcept {
        return !stop_requested_ && next_unsent_offset_ < bytes_.size() && next_unsent_offset_ >= max_data_;
    }

    [[nodiscard]] bool has_unsent_data() const noexcept {
        return !stop_requested_ && next_unsent_offset_ < bytes_.size();
    }

    [[nodiscard]] bool has_retransmittable_data() const noexcept {
        if (stop_requested_) {
            return false;
        }
        for (const auto& range : lost_) {
            if (!is_acked(range)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::optional<stream_data_blocked_frame> blocked_frame() const noexcept {
        if (!blocked()) {
            return std::nullopt;
        }
        return stream_data_blocked_frame{stream_id_, max_data_};
    }

    [[nodiscard]] bool finished() const noexcept {
        return finished_;
    }

    [[nodiscard]] bool fin_sent() const noexcept {
        return fin_sent_;
    }

    [[nodiscard]] bool fin_acked() const noexcept {
        return fin_acked_;
    }

    [[nodiscard]] bool closed() const noexcept {
        return finished_ && fin_acked_;
    }

    [[nodiscard]] bool stop_requested() const noexcept {
        return stop_requested_;
    }

    [[nodiscard]] std::uint64_t stop_error_code() const noexcept {
        return stop_error_code_.value_or(0);
    }

private:
    std::uint64_t stream_id_{};
    std::uint64_t max_data_{std::numeric_limits<std::uint64_t>::max()};
    std::vector<std::byte> bytes_{};
    std::uint64_t next_unsent_offset_{};
    bool finished_{};
    bool fin_sent_{};
    bool fin_acked_{};
    bool stop_requested_{};
    std::optional<std::uint64_t> stop_error_code_{};
    std::vector<stream_send_range> lost_{};
    std::vector<stream_send_range> acked_{};

    [[nodiscard]] bool is_acked(stream_send_range range) const noexcept {
        for (const auto& acked : acked_) {
            if (range_covers(acked, range)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool covers_lost_fin(stream_send_range range) const noexcept {
        for (const auto& lost : lost_) {
            if (lost.fin && range_covers(range, lost)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static bool range_covers(stream_send_range covering, stream_send_range covered) noexcept {
        const auto covering_end = covering.offset + covering.length;
        const auto covered_end = covered.offset + covered.length;
        const auto bytes_covered = covering.offset <= covered.offset && covering_end >= covered_end;
        const auto fin_covered = !covered.fin || covering.fin;
        return bytes_covered && fin_covered;
    }

    [[nodiscard]] bool valid_sent_range(stream_send_range range) const noexcept {
        if (range.offset > static_cast<std::uint64_t>(bytes_.size())) {
            return false;
        }
        if (range.length > static_cast<std::uint64_t>(bytes_.size()) - range.offset) {
            return false;
        }
        if (range.offset + range.length > next_unsent_offset_) {
            return false;
        }
        if (range.fin && (!finished_ || range.offset + range.length != bytes_.size())) {
            return false;
        }
        return true;
    }

    [[nodiscard]] stream_send_result make_frame(stream_send_range range) const {
        std::vector<std::byte> data;
        data.reserve(static_cast<std::size_t>(range.length));
        const auto start = static_cast<std::size_t>(range.offset);
        for (std::size_t index = 0; index < static_cast<std::size_t>(range.length); ++index) {
            data.push_back(bytes_[start + index]);
        }

        return {
            stream_frame{stream_id_, range.offset, range.offset != 0, true, range.fin, flowq::buffer{data}},
            range,
            true,
            {}
        };
    }
};

class stream_send_set {
public:
    explicit stream_send_set(
        std::uint64_t initial_max_data = std::numeric_limits<std::uint64_t>::max(),
        stream_limits limits = {}) noexcept
        : initial_max_data_{initial_max_data}, limits_{limits} {}

    [[nodiscard]] stream_operation_result append(std::uint64_t stream_id, const flowq::buffer& data) {
        auto* state = ensure_state_for(stream_id);
        if (state == nullptr) {
            return {detail::stream_error("peer stream count limit exceeded")};
        }
        return state->append(data);
    }

    [[nodiscard]] stream_operation_result finish(std::uint64_t stream_id) {
        auto* state = ensure_state_for(stream_id);
        if (state == nullptr) {
            return {detail::stream_error("peer stream count limit exceeded")};
        }
        return state->finish();
    }

    [[nodiscard]] stream_send_result pop_frame(std::uint64_t stream_id, std::size_t max_data_size) {
        auto* state = find(stream_id);
        return state == nullptr ? stream_send_result{} : state->pop_frame(max_data_size);
    }

    [[nodiscard]] stream_frame_schedule_result pop_frames(
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

            auto stream_result = pop_frame(stream_id, max_stream_data_size);
            if (!stream_result.ok()) {
                result.error = stream_result.error;
                return result;
            }

            if (stream_result.has_frame) {
                result.frames.push_back(frame{std::move(stream_result.frame)});
                continue;
            }

            auto blocked = blocked_frame(stream_id);
            if (blocked.has_value()) {
                result.frames.push_back(frame{*blocked});
            }
        }
        return result;
    }

    void update_max_data(std::uint64_t stream_id, std::uint64_t max_data) {
        state_for(stream_id).update_max_data(max_data);
    }

    void update_max_data(const max_stream_data_frame& frame) {
        update_max_data(frame.stream_id, frame.maximum_stream_data);
    }

    void update_max_streams(const max_streams_frame& frame) noexcept {
        auto& limit = mutable_limit_for(frame.direction);
        limit = std::max(limit, frame.maximum_streams);
    }

    [[nodiscard]] stream_operation_result stop_sending(const stop_sending_frame& frame) {
        auto* state = find(frame.stream_id);
        if (state == nullptr) {
            return {};
        }
        return state->stop_sending(frame);
    }

    void on_acked(std::uint64_t stream_id, stream_send_range range) {
        if (auto* state = find(stream_id)) {
            state->on_acked(range);
        }
    }

    void on_lost(std::uint64_t stream_id, stream_send_range range) {
        if (auto* state = find(stream_id)) {
            state->on_lost(range);
        }
    }

    [[nodiscard]] std::optional<stream_data_blocked_frame> blocked_frame(std::uint64_t stream_id) const noexcept {
        const auto found = streams_.find(stream_id);
        if (found == streams_.end()) {
            return std::nullopt;
        }
        return found->second.blocked_frame();
    }

    [[nodiscard]] std::optional<streams_blocked_frame> streams_blocked_frame(stream_direction direction) const noexcept {
        const auto blocked = direction == stream_direction::bidirectional ? bidi_streams_blocked_ : uni_streams_blocked_;
        if (!blocked) {
            return std::nullopt;
        }
        return flowq::quic::streams_blocked_frame{direction, limit_for(direction)};
    }

    [[nodiscard]] bool has_unsent_data(std::uint64_t stream_id) const noexcept {
        const auto found = streams_.find(stream_id);
        return found != streams_.end() && found->second.has_unsent_data();
    }

    [[nodiscard]] bool has_retransmittable_data(std::uint64_t stream_id) const noexcept {
        const auto found = streams_.find(stream_id);
        return found != streams_.end() && found->second.has_retransmittable_data();
    }

    [[nodiscard]] stream_send_state* find(std::uint64_t stream_id) noexcept {
        const auto found = streams_.find(stream_id);
        return found == streams_.end() ? nullptr : &found->second;
    }

    [[nodiscard]] const stream_send_state* find(std::uint64_t stream_id) const noexcept {
        const auto found = streams_.find(stream_id);
        return found == streams_.end() ? nullptr : &found->second;
    }

private:
    std::uint64_t initial_max_data_{std::numeric_limits<std::uint64_t>::max()};
    stream_limits limits_{};
    std::map<std::uint64_t, stream_send_state> streams_{};
    bool bidi_streams_blocked_{};
    bool uni_streams_blocked_{};

    [[nodiscard]] stream_send_state& state_for(std::uint64_t stream_id) {
        auto [iterator, inserted] = streams_.try_emplace(stream_id, stream_id, initial_max_data_);
        (void)inserted;
        return iterator->second;
    }

    [[nodiscard]] stream_send_state* ensure_state_for(std::uint64_t stream_id) {
        if (auto* existing = find(stream_id)) {
            return existing;
        }
        const auto info = classify_stream_id(stream_id);
        const auto ordinal = stream_id >> 2U;
        if (ordinal >= limit_for(info.direction)) {
            mark_streams_blocked(info.direction);
            return nullptr;
        }
        return &state_for(stream_id);
    }

    [[nodiscard]] std::uint64_t limit_for(stream_direction direction) const noexcept {
        return direction == stream_direction::bidirectional ? limits_.bidirectional : limits_.unidirectional;
    }

    [[nodiscard]] std::uint64_t& mutable_limit_for(stream_direction direction) noexcept {
        return direction == stream_direction::bidirectional ? limits_.bidirectional : limits_.unidirectional;
    }

    void mark_streams_blocked(stream_direction direction) noexcept {
        if (direction == stream_direction::bidirectional) {
            bidi_streams_blocked_ = true;
        } else {
            uni_streams_blocked_ = true;
        }
    }
};

} // namespace flowq::quic
