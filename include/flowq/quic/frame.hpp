#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>
#include <flowq/quic/varint.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace flowq::quic {

struct padding_frame {
    std::size_t count{};
};

struct ping_frame {};

struct connection_close_frame {
    std::uint64_t error_code{};
    std::uint64_t frame_type{};
    std::string reason;
};

struct ack_range {
    std::uint64_t gap{};
    std::uint64_t length{};
};

struct ack_frame {
    std::uint64_t largest_acknowledged{};
    std::uint64_t ack_delay{};
    std::uint64_t first_ack_range{};
    std::vector<ack_range> ranges;
};

struct crypto_frame {
    std::uint64_t offset{};
    flowq::buffer data;
};

struct stream_frame {
    std::uint64_t stream_id{};
    std::uint64_t offset{};
    bool offset_present{};
    bool length_present{};
    bool fin{};
    flowq::buffer data;
};

struct max_data_frame {
    std::uint64_t maximum_data{};
};

struct max_stream_data_frame {
    std::uint64_t stream_id{};
    std::uint64_t maximum_stream_data{};
};

struct data_blocked_frame {
    std::uint64_t maximum_data{};
};

struct stream_data_blocked_frame {
    std::uint64_t stream_id{};
    std::uint64_t maximum_stream_data{};
};

using frame = std::variant<
    padding_frame,
    ping_frame,
    connection_close_frame,
    ack_frame,
    crypto_frame,
    stream_frame,
    max_data_frame,
    max_stream_data_frame,
    data_blocked_frame,
    stream_data_blocked_frame>;

struct frame_decode_result {
    std::vector<frame> frames;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

struct frame_encode_result {
    flowq::buffer payload;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

namespace detail {

[[nodiscard]] inline bool append_varint(std::vector<std::byte>& output, std::uint64_t value) {
    std::array<std::byte, 8> encoded{};
    auto result = encode_varint(value, encoded);
    if (!result.ok()) {
        return false;
    }

    output.insert(output.end(), encoded.begin(), encoded.begin() + static_cast<std::ptrdiff_t>(result.bytes_written));
    return true;
}

[[nodiscard]] inline frame_encode_result encode_connection_close(const connection_close_frame& frame) {
    std::vector<std::byte> output;
    if (!append_varint(output, 0x1c) || !append_varint(output, frame.error_code) || !append_varint(output, frame.frame_type)) {
        return {{}, codec_error("failed to encode CONNECTION_CLOSE frame")};
    }

    if (!append_varint(output, frame.reason.size())) {
        return {{}, codec_error("failed to encode CONNECTION_CLOSE reason length")};
    }

    for (auto character : frame.reason) {
        output.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }

    return {flowq::buffer{output}, {}};
}

inline void append_buffer(std::vector<std::byte>& output, const flowq::buffer& buffer) {
    output.insert(output.end(), buffer.data(), buffer.data() + static_cast<std::ptrdiff_t>(buffer.size()));
}

[[nodiscard]] inline frame_encode_result encode_ack(const ack_frame& frame) {
    std::vector<std::byte> output;
    if (!append_varint(output, 0x02) || !append_varint(output, frame.largest_acknowledged) || !append_varint(output, frame.ack_delay) ||
        !append_varint(output, frame.ranges.size()) || !append_varint(output, frame.first_ack_range)) {
        return {{}, codec_error("failed to encode ACK frame")};
    }

    for (const auto& range : frame.ranges) {
        if (!append_varint(output, range.gap) || !append_varint(output, range.length)) {
            return {{}, codec_error("failed to encode ACK range")};
        }
    }

    return {flowq::buffer{output}, {}};
}

[[nodiscard]] inline frame_encode_result encode_crypto(const crypto_frame& frame) {
    std::vector<std::byte> output;
    if (!append_varint(output, 0x06) || !append_varint(output, frame.offset) || !append_varint(output, frame.data.size())) {
        return {{}, codec_error("failed to encode CRYPTO frame")};
    }
    append_buffer(output, frame.data);
    return {flowq::buffer{output}, {}};
}

[[nodiscard]] inline frame_encode_result encode_stream(const stream_frame& frame) {
    const auto type = 0x08U | (frame.offset_present ? 0x04U : 0x00U) | (frame.length_present ? 0x02U : 0x00U) |
        (frame.fin ? 0x01U : 0x00U);
    std::vector<std::byte> output;
    if (!append_varint(output, type) || !append_varint(output, frame.stream_id)) {
        return {{}, codec_error("failed to encode STREAM frame")};
    }

    if (frame.offset_present && !append_varint(output, frame.offset)) {
        return {{}, codec_error("failed to encode STREAM offset")};
    }

    if (frame.length_present && !append_varint(output, frame.data.size())) {
        return {{}, codec_error("failed to encode STREAM length")};
    }

    append_buffer(output, frame.data);
    return {flowq::buffer{output}, {}};
}

[[nodiscard]] inline frame_encode_result encode_single_limit_frame(std::uint64_t type, std::uint64_t maximum_data, const char* message) {
    std::vector<std::byte> output;
    if (!append_varint(output, type) || !append_varint(output, maximum_data)) {
        return {{}, codec_error(message)};
    }
    return {flowq::buffer{output}, {}};
}

[[nodiscard]] inline frame_encode_result encode_stream_limit_frame(
    std::uint64_t type,
    std::uint64_t stream_id,
    std::uint64_t maximum_stream_data,
    const char* message) {
    std::vector<std::byte> output;
    if (!append_varint(output, type) || !append_varint(output, stream_id) || !append_varint(output, maximum_stream_data)) {
        return {{}, codec_error(message)};
    }
    return {flowq::buffer{output}, {}};
}

[[nodiscard]] inline bool read_varint_at(std::span<const std::byte> input, std::size_t& offset, std::uint64_t& value) {
    auto decoded = decode_varint(input.subspan(offset));
    if (!decoded.ok()) {
        return false;
    }

    value = decoded.value;
    offset += decoded.bytes_read;
    return true;
}

} // namespace detail

[[nodiscard]] inline frame_encode_result encode_frame(const padding_frame& frame) {
    std::vector<std::byte> output(frame.count, std::byte{0x00});
    return {flowq::buffer{output}, {}};
}

[[nodiscard]] inline frame_encode_result encode_frame(const ping_frame&) {
    std::vector<std::byte> output{std::byte{0x01}};
    return {flowq::buffer{output}, {}};
}

[[nodiscard]] inline frame_encode_result encode_frame(const connection_close_frame& frame) {
    return detail::encode_connection_close(frame);
}

[[nodiscard]] inline frame_encode_result encode_frame(const ack_frame& frame) {
    return detail::encode_ack(frame);
}

[[nodiscard]] inline frame_encode_result encode_frame(const crypto_frame& frame) {
    return detail::encode_crypto(frame);
}

[[nodiscard]] inline frame_encode_result encode_frame(const stream_frame& frame) {
    return detail::encode_stream(frame);
}

[[nodiscard]] inline frame_encode_result encode_frame(const max_data_frame& frame) {
    return detail::encode_single_limit_frame(0x10, frame.maximum_data, "failed to encode MAX_DATA frame");
}

[[nodiscard]] inline frame_encode_result encode_frame(const max_stream_data_frame& frame) {
    return detail::encode_stream_limit_frame(0x11, frame.stream_id, frame.maximum_stream_data, "failed to encode MAX_STREAM_DATA frame");
}

[[nodiscard]] inline frame_encode_result encode_frame(const data_blocked_frame& frame) {
    return detail::encode_single_limit_frame(0x14, frame.maximum_data, "failed to encode DATA_BLOCKED frame");
}

[[nodiscard]] inline frame_encode_result encode_frame(const stream_data_blocked_frame& frame) {
    return detail::encode_stream_limit_frame(0x15, frame.stream_id, frame.maximum_stream_data, "failed to encode STREAM_DATA_BLOCKED frame");
}

[[nodiscard]] inline frame_decode_result decode_frames(std::span<const std::byte> input) {
    if (input.empty()) {
        return {{}, codec_error("QUIC packet payload contains no frames")};
    }

    std::vector<frame> frames;
    std::size_t offset = 0;
    while (offset < input.size()) {
        std::uint64_t type = 0;
        if (!detail::read_varint_at(input, offset, type)) {
            return {{}, codec_error("truncated QUIC frame type")};
        }

        if (type == 0x00) {
            std::size_t count = 1;
            while (offset < input.size() && input[offset] == std::byte{0x00}) {
                ++count;
                ++offset;
            }
            frames.emplace_back(padding_frame{count});
            continue;
        }

        if (type == 0x01) {
            frames.emplace_back(ping_frame{});
            continue;
        }

        if (type == 0x02) {
            std::uint64_t largest_acknowledged = 0;
            std::uint64_t ack_delay = 0;
            std::uint64_t range_count = 0;
            std::uint64_t first_ack_range = 0;
            if (!detail::read_varint_at(input, offset, largest_acknowledged) || !detail::read_varint_at(input, offset, ack_delay) ||
                !detail::read_varint_at(input, offset, range_count) || !detail::read_varint_at(input, offset, first_ack_range)) {
                return {{}, codec_error("truncated ACK frame")};
            }

            std::vector<ack_range> ranges;
            ranges.reserve(static_cast<std::size_t>(range_count));
            for (std::uint64_t index = 0; index < range_count; ++index) {
                std::uint64_t gap = 0;
                std::uint64_t length = 0;
                if (!detail::read_varint_at(input, offset, gap) || !detail::read_varint_at(input, offset, length)) {
                    return {{}, codec_error("truncated ACK range")};
                }
                ranges.push_back(ack_range{gap, length});
            }

            frames.emplace_back(ack_frame{largest_acknowledged, ack_delay, first_ack_range, std::move(ranges)});
            continue;
        }

        if (type == 0x03) {
            return {{}, codec_error("ACK ECN frame is unsupported in M2c")};
        }

        if (type == 0x06) {
            std::uint64_t crypto_offset = 0;
            std::uint64_t length = 0;
            if (!detail::read_varint_at(input, offset, crypto_offset) || !detail::read_varint_at(input, offset, length)) {
                return {{}, codec_error("truncated CRYPTO frame")};
            }

            if (length > input.size() - offset) {
                return {{}, codec_error("truncated CRYPTO data")};
            }

            auto data = flowq::buffer{input.subspan(offset, static_cast<std::size_t>(length))};
            offset += static_cast<std::size_t>(length);
            frames.emplace_back(crypto_frame{crypto_offset, std::move(data)});
            continue;
        }

        if (type >= 0x08 && type <= 0x0f) {
            std::uint64_t stream_id = 0;
            if (!detail::read_varint_at(input, offset, stream_id)) {
                return {{}, codec_error("truncated STREAM id")};
            }

            const auto stream_type = static_cast<unsigned int>(type);
            const bool offset_present = (stream_type & 0x04U) != 0;
            const bool length_present = (stream_type & 0x02U) != 0;
            const bool fin = (stream_type & 0x01U) != 0;

            std::uint64_t stream_offset = 0;
            if (offset_present && !detail::read_varint_at(input, offset, stream_offset)) {
                return {{}, codec_error("truncated STREAM offset")};
            }

            std::uint64_t length = input.size() - offset;
            if (length_present && !detail::read_varint_at(input, offset, length)) {
                return {{}, codec_error("truncated STREAM length")};
            }

            if (length > input.size() - offset) {
                return {{}, codec_error("truncated STREAM data")};
            }

            auto data = flowq::buffer{input.subspan(offset, static_cast<std::size_t>(length))};
            offset += static_cast<std::size_t>(length);
            frames.emplace_back(stream_frame{stream_id, stream_offset, offset_present, length_present, fin, std::move(data)});
            continue;
        }

        if (type == 0x10) {
            std::uint64_t maximum_data = 0;
            if (!detail::read_varint_at(input, offset, maximum_data)) {
                return {{}, codec_error("truncated MAX_DATA frame")};
            }
            frames.emplace_back(max_data_frame{maximum_data});
            continue;
        }

        if (type == 0x11) {
            std::uint64_t stream_id = 0;
            std::uint64_t maximum_stream_data = 0;
            if (!detail::read_varint_at(input, offset, stream_id) || !detail::read_varint_at(input, offset, maximum_stream_data)) {
                return {{}, codec_error("truncated MAX_STREAM_DATA frame")};
            }
            frames.emplace_back(max_stream_data_frame{stream_id, maximum_stream_data});
            continue;
        }

        if (type == 0x14) {
            std::uint64_t maximum_data = 0;
            if (!detail::read_varint_at(input, offset, maximum_data)) {
                return {{}, codec_error("truncated DATA_BLOCKED frame")};
            }
            frames.emplace_back(data_blocked_frame{maximum_data});
            continue;
        }

        if (type == 0x15) {
            std::uint64_t stream_id = 0;
            std::uint64_t maximum_stream_data = 0;
            if (!detail::read_varint_at(input, offset, stream_id) || !detail::read_varint_at(input, offset, maximum_stream_data)) {
                return {{}, codec_error("truncated STREAM_DATA_BLOCKED frame")};
            }
            frames.emplace_back(stream_data_blocked_frame{stream_id, maximum_stream_data});
            continue;
        }

        if (type == 0x1c) {
            std::uint64_t error_code = 0;
            std::uint64_t frame_type = 0;
            std::uint64_t reason_size = 0;
            if (!detail::read_varint_at(input, offset, error_code) || !detail::read_varint_at(input, offset, frame_type) ||
                !detail::read_varint_at(input, offset, reason_size)) {
                return {{}, codec_error("truncated CONNECTION_CLOSE frame")};
            }

            if (reason_size > input.size() - offset) {
                return {{}, codec_error("truncated CONNECTION_CLOSE reason phrase")};
            }

            std::string reason;
            reason.reserve(static_cast<std::size_t>(reason_size));
            for (std::uint64_t index = 0; index < reason_size; ++index) {
                reason.push_back(static_cast<char>(input[offset + static_cast<std::size_t>(index)]));
            }
            offset += static_cast<std::size_t>(reason_size);
            frames.emplace_back(connection_close_frame{error_code, frame_type, std::move(reason)});
            continue;
        }

        return {{}, codec_error("unsupported QUIC frame type")};
    }

    return {std::move(frames), {}};
}

[[nodiscard]] inline frame_decode_result decode_frames(const flowq::buffer& input) {
    return decode_frames(std::span<const std::byte>{input.data(), input.size()});
}

template <std::ranges::contiguous_range Range>
    requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<Range>>, std::byte>
[[nodiscard]] auto decode_frames(const Range& input) {
    return decode_frames(std::span<const std::byte>{std::ranges::data(input), std::ranges::size(input)});
}

} // namespace flowq::quic
