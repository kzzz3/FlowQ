#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>
#include <flowq/quic/frame.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
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

enum class stream_direction {
    bidirectional,
    unidirectional
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

inline void append_stream_buffer(std::vector<std::byte>& output, const flowq::buffer& input) {
    output.insert(output.end(), input.data(), input.data() + static_cast<std::ptrdiff_t>(input.size()));
}

} // namespace detail

class stream_receive_state {
public:
    [[nodiscard]] stream_receive_result receive(const stream_frame& frame) {
        const auto offset = frame.offset_present ? frame.offset : 0;
        const auto frame_size = static_cast<std::uint64_t>(frame.data.size());
        if (offset > UINT64_MAX - frame_size) {
            return failure("STREAM frame offset overflows");
        }
        const auto end = offset + frame_size;

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
        return final_size_.has_value() && next_offset_ == *final_size_;
    }

private:
    std::uint64_t next_offset_{};
    std::vector<std::byte> delivered_{};
    std::map<std::uint64_t, flowq::buffer> pending_{};
    std::optional<std::uint64_t> final_size_{};

    [[nodiscard]] std::uint64_t final_size_value() const noexcept {
        return final_size_.value_or(0);
    }

    [[nodiscard]] stream_receive_result success(flowq::buffer data) const {
        return {std::move(data), final_size_.has_value(), final_size_value(), closed(), {}};
    }

    [[nodiscard]] stream_receive_result failure(const char* message) const {
        return {{}, final_size_.has_value(), final_size_value(), closed(), detail::stream_error(message)};
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
    [[nodiscard]] stream_delivery receive(const stream_frame& frame) {
        auto& state = streams_[frame.stream_id];
        return {frame.stream_id, state.receive(frame)};
    }

    [[nodiscard]] const stream_receive_state* find(std::uint64_t stream_id) const noexcept {
        const auto found = streams_.find(stream_id);
        return found == streams_.end() ? nullptr : &found->second;
    }

private:
    std::map<std::uint64_t, stream_receive_state> streams_{};
};

} // namespace flowq::quic
