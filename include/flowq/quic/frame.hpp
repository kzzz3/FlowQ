#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>
#include <flowq/quic/packet_header.hpp>
#include <flowq/quic/varint.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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

struct application_close_frame {
    std::uint64_t error_code{};
    std::string reason;
};

struct reset_stream_frame {
    std::uint64_t stream_id{};
    std::uint64_t application_error_code{};
    std::uint64_t final_size{};
};

struct stop_sending_frame {
    std::uint64_t stream_id{};
    std::uint64_t application_error_code{};
};

struct ack_range {
    std::uint64_t gap{};
    std::uint64_t length{};
};

struct ack_ecn_counts {
    std::uint64_t ect0{};
    std::uint64_t ect1{};
    std::uint64_t ce{};
};

struct ack_frame {
    std::uint64_t largest_acknowledged{};
    std::uint64_t ack_delay{};
    std::uint64_t first_ack_range{};
    std::vector<ack_range> ranges;
    std::optional<ack_ecn_counts> ecn_counts;
};

struct crypto_frame {
    std::uint64_t offset{};
    flowq::buffer data;
};

struct new_token_frame {
    flowq::buffer token;
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

enum class stream_direction {
    bidirectional,
    unidirectional
};

struct max_streams_frame {
    stream_direction direction{};
    std::uint64_t maximum_streams{};
};

struct streams_blocked_frame {
    stream_direction direction{};
    std::uint64_t maximum_streams{};
};

struct new_connection_id_frame {
    std::uint64_t sequence_number{};
    std::uint64_t retire_prior_to{};
    connection_id connection_id;
    flowq::buffer stateless_reset_token;
};

struct retire_connection_id_frame {
    std::uint64_t sequence_number{};
};

struct handshake_done_frame {};

struct path_challenge_frame {
    std::array<std::byte, 8> data{};
};

struct path_response_frame {
    std::array<std::byte, 8> data{};
};

using frame = std::variant<
    padding_frame,
    ping_frame,
    connection_close_frame,
    application_close_frame,
    reset_stream_frame,
    stop_sending_frame,
    ack_frame,
    crypto_frame,
    new_token_frame,
    stream_frame,
    max_data_frame,
    max_stream_data_frame,
    data_blocked_frame,
    stream_data_blocked_frame,
    max_streams_frame,
    streams_blocked_frame,
    new_connection_id_frame,
    retire_connection_id_frame,
    handshake_done_frame,
    path_challenge_frame,
    path_response_frame>;

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

[[nodiscard]] inline frame_encode_result encode_application_close(const application_close_frame& frame) {
    std::vector<std::byte> output;
    if (!append_varint(output, 0x1d) || !append_varint(output, frame.error_code)) {
        return {{}, codec_error("failed to encode APPLICATION_CLOSE frame")};
    }

    if (!append_varint(output, frame.reason.size())) {
        return {{}, codec_error("failed to encode APPLICATION_CLOSE reason length")};
    }

    for (auto character : frame.reason) {
        output.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }

    return {flowq::buffer{output}, {}};
}

[[nodiscard]] inline frame_encode_result encode_reset_stream(const reset_stream_frame& frame) {
    std::vector<std::byte> output;
    if (!append_varint(output, 0x04) || !append_varint(output, frame.stream_id) || !append_varint(output, frame.application_error_code) ||
        !append_varint(output, frame.final_size)) {
        return {{}, codec_error("failed to encode RESET_STREAM frame")};
    }
    return {flowq::buffer{output}, {}};
}

[[nodiscard]] inline frame_encode_result encode_stop_sending(const stop_sending_frame& frame) {
    std::vector<std::byte> output;
    if (!append_varint(output, 0x05) || !append_varint(output, frame.stream_id) || !append_varint(output, frame.application_error_code)) {
        return {{}, codec_error("failed to encode STOP_SENDING frame")};
    }
    return {flowq::buffer{output}, {}};
}

inline void append_buffer(std::vector<std::byte>& output, const flowq::buffer& buffer) {
    output.insert(output.end(), buffer.data(), buffer.data() + static_cast<std::ptrdiff_t>(buffer.size()));
}

[[nodiscard]] inline frame_encode_result encode_ack(const ack_frame& frame) {
    std::vector<std::byte> output;
    const auto type = frame.ecn_counts.has_value() ? 0x03 : 0x02;
    if (!append_varint(output, type) || !append_varint(output, frame.largest_acknowledged) || !append_varint(output, frame.ack_delay) ||
        !append_varint(output, frame.ranges.size()) || !append_varint(output, frame.first_ack_range)) {
        return {{}, codec_error("failed to encode ACK frame")};
    }

    for (const auto& range : frame.ranges) {
        if (!append_varint(output, range.gap) || !append_varint(output, range.length)) {
            return {{}, codec_error("failed to encode ACK range")};
        }
    }

    if (frame.ecn_counts.has_value()) {
        const auto& counts = *frame.ecn_counts;
        if (!append_varint(output, counts.ect0) || !append_varint(output, counts.ect1) || !append_varint(output, counts.ce)) {
            return {{}, codec_error("failed to encode ACK ECN counts")};
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

[[nodiscard]] inline frame_encode_result encode_new_token(const new_token_frame& frame) {
    if (frame.token.empty()) {
        return {{}, codec_error("NEW_TOKEN token must not be empty")};
    }

    std::vector<std::byte> output;
    if (!append_varint(output, 0x07) || !append_varint(output, frame.token.size())) {
        return {{}, codec_error("failed to encode NEW_TOKEN frame")};
    }
    append_buffer(output, frame.token);
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

[[nodiscard]] inline frame_encode_result encode_stream_count_frame(
    std::uint64_t bidirectional_type,
    std::uint64_t unidirectional_type,
    stream_direction direction,
    std::uint64_t maximum_streams,
    const char* message) {
    std::vector<std::byte> output;
    const auto type = direction == stream_direction::bidirectional ? bidirectional_type : unidirectional_type;
    if (!append_varint(output, type) || !append_varint(output, maximum_streams)) {
        return {{}, codec_error(message)};
    }
    return {flowq::buffer{output}, {}};
}

[[nodiscard]] inline frame_encode_result encode_new_connection_id(const new_connection_id_frame& frame) {
    if (frame.connection_id.bytes.empty() || frame.connection_id.bytes.size() > 20) {
        return {{}, codec_error("NEW_CONNECTION_ID connection ID length must be 1 to 20 bytes")};
    }
    if (frame.stateless_reset_token.size() != 16) {
        return {{}, codec_error("NEW_CONNECTION_ID stateless reset token must be 16 bytes")};
    }

    std::vector<std::byte> output;
    if (!append_varint(output, 0x18) ||
        !append_varint(output, frame.sequence_number) ||
        !append_varint(output, frame.retire_prior_to) ||
        !append_varint(output, frame.connection_id.bytes.size())) {
        return {{}, codec_error("failed to encode NEW_CONNECTION_ID frame")};
    }
    append_buffer(output, frame.connection_id.bytes);
    append_buffer(output, frame.stateless_reset_token);
    return {flowq::buffer{output}, {}};
}

[[nodiscard]] inline frame_encode_result encode_path_validation_frame(
    std::uint64_t type,
    const std::array<std::byte, 8>& data,
    const char* message) {
    std::vector<std::byte> output;
    if (!append_varint(output, type)) {
        return {{}, codec_error(message)};
    }
    output.insert(output.end(), data.begin(), data.end());
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

[[nodiscard]] inline bool read_path_validation_data(
    std::span<const std::byte> input,
    std::size_t& offset,
    std::array<std::byte, 8>& data) {
    if (input.size() - offset < data.size()) {
        return false;
    }
    for (std::size_t index = 0; index < data.size(); ++index) {
        data[index] = input[offset + index];
    }
    offset += data.size();
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

[[nodiscard]] inline frame_encode_result encode_frame(const application_close_frame& frame) {
    return detail::encode_application_close(frame);
}

[[nodiscard]] inline frame_encode_result encode_frame(const reset_stream_frame& frame) {
    return detail::encode_reset_stream(frame);
}

[[nodiscard]] inline frame_encode_result encode_frame(const stop_sending_frame& frame) {
    return detail::encode_stop_sending(frame);
}

[[nodiscard]] inline frame_encode_result encode_frame(const ack_frame& frame) {
    return detail::encode_ack(frame);
}

[[nodiscard]] inline frame_encode_result encode_frame(const crypto_frame& frame) {
    return detail::encode_crypto(frame);
}

[[nodiscard]] inline frame_encode_result encode_frame(const new_token_frame& frame) {
    return detail::encode_new_token(frame);
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

[[nodiscard]] inline frame_encode_result encode_frame(const max_streams_frame& frame) {
    return detail::encode_stream_count_frame(0x12, 0x13, frame.direction, frame.maximum_streams, "failed to encode MAX_STREAMS frame");
}

[[nodiscard]] inline frame_encode_result encode_frame(const streams_blocked_frame& frame) {
    return detail::encode_stream_count_frame(0x16, 0x17, frame.direction, frame.maximum_streams, "failed to encode STREAMS_BLOCKED frame");
}

[[nodiscard]] inline frame_encode_result encode_frame(const new_connection_id_frame& frame) {
    return detail::encode_new_connection_id(frame);
}

[[nodiscard]] inline frame_encode_result encode_frame(const retire_connection_id_frame& frame) {
    return detail::encode_single_limit_frame(0x19, frame.sequence_number, "failed to encode RETIRE_CONNECTION_ID frame");
}

[[nodiscard]] inline frame_encode_result encode_frame(const handshake_done_frame&) {
    std::vector<std::byte> output{std::byte{0x1e}};
    return {flowq::buffer{output}, {}};
}

[[nodiscard]] inline frame_encode_result encode_frame(const path_challenge_frame& frame) {
    return detail::encode_path_validation_frame(0x1a, frame.data, "failed to encode PATH_CHALLENGE frame");
}

[[nodiscard]] inline frame_encode_result encode_frame(const path_response_frame& frame) {
    return detail::encode_path_validation_frame(0x1b, frame.data, "failed to encode PATH_RESPONSE frame");
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

        if (type == 0x02 || type == 0x03) {
            std::uint64_t largest_acknowledged = 0;
            std::uint64_t ack_delay = 0;
            std::uint64_t range_count = 0;
            std::uint64_t first_ack_range = 0;
            if (!detail::read_varint_at(input, offset, largest_acknowledged) || !detail::read_varint_at(input, offset, ack_delay) ||
                !detail::read_varint_at(input, offset, range_count) || !detail::read_varint_at(input, offset, first_ack_range)) {
                return {{}, codec_error("truncated ACK frame")};
            }

            const auto remaining = input.size() - offset;
            if (range_count > static_cast<std::uint64_t>(remaining / 2)) {
                return {{}, codec_error("truncated ACK range")};
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

            std::optional<ack_ecn_counts> ecn_counts;
            if (type == 0x03) {
                ack_ecn_counts counts{};
                if (!detail::read_varint_at(input, offset, counts.ect0) || !detail::read_varint_at(input, offset, counts.ect1) ||
                    !detail::read_varint_at(input, offset, counts.ce)) {
                    return {{}, codec_error("truncated ACK ECN counts")};
                }
                ecn_counts = counts;
            }

            frames.emplace_back(ack_frame{largest_acknowledged, ack_delay, first_ack_range, std::move(ranges), std::move(ecn_counts)});
            continue;
        }

        if (type == 0x04) {
            std::uint64_t stream_id = 0;
            std::uint64_t application_error_code = 0;
            std::uint64_t final_size = 0;
            if (!detail::read_varint_at(input, offset, stream_id) || !detail::read_varint_at(input, offset, application_error_code) ||
                !detail::read_varint_at(input, offset, final_size)) {
                return {{}, codec_error("truncated RESET_STREAM frame")};
            }
            frames.emplace_back(reset_stream_frame{stream_id, application_error_code, final_size});
            continue;
        }

        if (type == 0x05) {
            std::uint64_t stream_id = 0;
            std::uint64_t application_error_code = 0;
            if (!detail::read_varint_at(input, offset, stream_id) || !detail::read_varint_at(input, offset, application_error_code)) {
                return {{}, codec_error("truncated STOP_SENDING frame")};
            }
            frames.emplace_back(stop_sending_frame{stream_id, application_error_code});
            continue;
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

        if (type == 0x07) {
            std::uint64_t length = 0;
            if (!detail::read_varint_at(input, offset, length)) {
                return {{}, codec_error("truncated NEW_TOKEN frame")};
            }
            if (length == 0) {
                return {{}, codec_error("NEW_TOKEN token must not be empty")};
            }
            if (length > input.size() - offset) {
                return {{}, codec_error("truncated NEW_TOKEN token")};
            }

            auto token = flowq::buffer{input.subspan(offset, static_cast<std::size_t>(length))};
            offset += static_cast<std::size_t>(length);
            frames.emplace_back(new_token_frame{std::move(token)});
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

        if (type == 0x12 || type == 0x13) {
            std::uint64_t maximum_streams = 0;
            if (!detail::read_varint_at(input, offset, maximum_streams)) {
                return {{}, codec_error("truncated MAX_STREAMS frame")};
            }
            frames.emplace_back(max_streams_frame{type == 0x12 ? stream_direction::bidirectional : stream_direction::unidirectional, maximum_streams});
            continue;
        }

        if (type == 0x16 || type == 0x17) {
            std::uint64_t maximum_streams = 0;
            if (!detail::read_varint_at(input, offset, maximum_streams)) {
                return {{}, codec_error("truncated STREAMS_BLOCKED frame")};
            }
            frames.emplace_back(streams_blocked_frame{type == 0x16 ? stream_direction::bidirectional : stream_direction::unidirectional, maximum_streams});
            continue;
        }

        if (type == 0x18) {
            std::uint64_t sequence_number = 0;
            std::uint64_t retire_prior_to = 0;
            std::uint64_t connection_id_length = 0;
            if (!detail::read_varint_at(input, offset, sequence_number) ||
                !detail::read_varint_at(input, offset, retire_prior_to) ||
                !detail::read_varint_at(input, offset, connection_id_length)) {
                return {{}, codec_error("truncated NEW_CONNECTION_ID frame")};
            }
            if (connection_id_length == 0 || connection_id_length > 20) {
                return {{}, codec_error("NEW_CONNECTION_ID connection ID length must be 1 to 20 bytes")};
            }
            if (connection_id_length + 16U > input.size() - offset) {
                return {{}, codec_error("truncated NEW_CONNECTION_ID frame")};
            }

            auto id = connection_id{flowq::buffer{input.subspan(offset, static_cast<std::size_t>(connection_id_length))}};
            offset += static_cast<std::size_t>(connection_id_length);
            auto token = flowq::buffer{input.subspan(offset, 16)};
            offset += 16;
            frames.emplace_back(new_connection_id_frame{sequence_number, retire_prior_to, std::move(id), std::move(token)});
            continue;
        }

        if (type == 0x19) {
            std::uint64_t sequence_number = 0;
            if (!detail::read_varint_at(input, offset, sequence_number)) {
                return {{}, codec_error("truncated RETIRE_CONNECTION_ID frame")};
            }
            frames.emplace_back(retire_connection_id_frame{sequence_number});
            continue;
        }

        if (type == 0x1a) {
            std::array<std::byte, 8> data{};
            if (!detail::read_path_validation_data(input, offset, data)) {
                return {{}, codec_error("truncated PATH_CHALLENGE frame")};
            }
            frames.emplace_back(path_challenge_frame{data});
            continue;
        }

        if (type == 0x1b) {
            std::array<std::byte, 8> data{};
            if (!detail::read_path_validation_data(input, offset, data)) {
                return {{}, codec_error("truncated PATH_RESPONSE frame")};
            }
            frames.emplace_back(path_response_frame{data});
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

        if (type == 0x1d) {
            std::uint64_t error_code = 0;
            std::uint64_t reason_size = 0;
            if (!detail::read_varint_at(input, offset, error_code) || !detail::read_varint_at(input, offset, reason_size)) {
                return {{}, codec_error("truncated APPLICATION_CLOSE frame")};
            }

            if (reason_size > input.size() - offset) {
                return {{}, codec_error("truncated APPLICATION_CLOSE reason phrase")};
            }

            std::string reason;
            reason.reserve(static_cast<std::size_t>(reason_size));
            for (std::uint64_t index = 0; index < reason_size; ++index) {
                reason.push_back(static_cast<char>(input[offset + static_cast<std::size_t>(index)]));
            }
            offset += static_cast<std::size_t>(reason_size);
            frames.emplace_back(application_close_frame{error_code, std::move(reason)});
            continue;
        }

        if (type == 0x1e) {
            frames.emplace_back(handshake_done_frame{});
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
