#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>
#include <flowq/quic/varint.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace flowq::quic::http3 {

/// HTTP/3 frame types (RFC 9114 §7.2).
enum class frame_type : std::uint64_t {
    data = 0x00,
    headers = 0x01,
    cancel_push = 0x03,
    settings = 0x04,
    push_promise = 0x05,
    goaway = 0x07,
    max_push_id = 0x0d
};

/// HTTP/3 error codes (RFC 9114 §8).
enum class error_code : std::uint64_t {
    no_error = 0x0100,
    general_protocol_error = 0x0101,
    internal_error = 0x0102,
    stream_creation_error = 0x0103,
    closed_critical_stream = 0x0104,
    frame_unexpected = 0x0105,
    frame_error = 0x0106,
    excessive_load = 0x0107,
    id_error = 0x0108,
    settings_error = 0x0109,
    missing_settings = 0x010a,
    request_rejected = 0x010b,
    request_cancelled = 0x010c,
    request_incomplete = 0x010d,
    message_error = 0x010e,
    connect_error = 0x010f,
    version_fallback = 0x0110
};

/// HTTP/3 settings (RFC 9114 §7.2.4).
enum class setting_id : std::uint64_t {
    max_field_section_size = 0x06,
    qpack_max_table_capacity = 0x01,
    qpack_blocked_streams = 0x07
};

/// HTTP/3 frame header.
struct frame_header {
    frame_type type{};
    std::uint64_t length{};
};

/// HTTP/3 settings container.
struct settings {
    std::uint64_t max_field_section_size{16384};
    std::uint64_t qpack_max_table_capacity{0};
    std::uint64_t qpack_blocked_streams{0};
};

/// HTTP/3 header field (name-value pair).
struct header_field {
    flowq::buffer name;
    flowq::buffer value;
};

/// HTTP/3 header block (list of header fields).
struct header_block {
    std::vector<header_field> fields;
};

/// HTTP/3 DATA frame.
struct data_frame {
    flowq::buffer data;
};

/// HTTP/3 HEADERS frame.
struct headers_frame {
    header_block headers;
};

/// HTTP/3 GOAWAY frame.
struct goaway_frame {
    std::uint64_t stream_id{};
};

/// HTTP/3 frame variant.
using http3_frame_variant = std::variant<data_frame, headers_frame, goaway_frame>;

/// Encode an HTTP/3 DATA frame.
[[nodiscard]] inline flowq::buffer encode_data_frame(const flowq::buffer& payload) {
    std::vector<std::byte> output;

    // Encode frame type (0x00 = DATA)
    std::byte type_buf[8]{};
    auto type_result = encode_varint(0x00, std::span<std::byte>{type_buf, 8});
    output.insert(output.end(), type_buf, type_buf + type_result.bytes_written);

    // Encode payload length
    std::byte length_buf[8]{};
    auto length_result = encode_varint(payload.size(), std::span<std::byte>{length_buf, 8});
    output.insert(output.end(), length_buf, length_buf + length_result.bytes_written);

    // Append payload
    output.insert(output.end(), payload.data(), payload.data() + payload.size());
    return flowq::buffer{output};
}

/// Encode an HTTP/3 GOAWAY frame.
[[nodiscard]] inline flowq::buffer encode_goaway_frame(std::uint64_t stream_id) {
    std::vector<std::byte> output;
    std::byte id_buf[8]{};
    auto id_result = encode_varint(stream_id, std::span<std::byte>{id_buf, 8});
    if (!id_result.ok()) {
        return flowq::buffer{};
    }

    // Encode frame type (0x07 = GOAWAY)
    std::byte type_buf[8]{};
    auto type_result = encode_varint(0x07, std::span<std::byte>{type_buf, 8});
    output.insert(output.end(), type_buf, type_buf + type_result.bytes_written);

    // Encode payload length to match the actual stream ID varint size.
    std::byte length_buf[8]{};
    auto length_result = encode_varint(id_result.bytes_written, std::span<std::byte>{length_buf, 8});
    output.insert(output.end(), length_buf, length_buf + length_result.bytes_written);

    // Encode stream ID
    output.insert(output.end(), id_buf, id_buf + id_result.bytes_written);

    return flowq::buffer{output};
}

/// Encode HTTP/3 SETTINGS frame.
[[nodiscard]] inline flowq::buffer encode_settings_frame(const settings& s) {
    std::vector<std::byte> output;

    // Encode frame type (0x04 = SETTINGS)
    std::byte type_buf[8]{};
    auto type_result = encode_varint(0x04, std::span<std::byte>{type_buf, 8});
    output.insert(output.end(), type_buf, type_buf + type_result.bytes_written);

    // Build settings payload
    std::vector<std::byte> payload;
    std::byte buf[8]{};

    // max_field_section_size
    auto id1 = encode_varint(static_cast<std::uint64_t>(setting_id::max_field_section_size), std::span<std::byte>{buf, 8});
    payload.insert(payload.end(), buf, buf + id1.bytes_written);
    auto val1 = encode_varint(s.max_field_section_size, std::span<std::byte>{buf, 8});
    payload.insert(payload.end(), buf, buf + val1.bytes_written);

    // qpack_max_table_capacity
    auto id2 = encode_varint(static_cast<std::uint64_t>(setting_id::qpack_max_table_capacity), std::span<std::byte>{buf, 8});
    payload.insert(payload.end(), buf, buf + id2.bytes_written);
    auto val2 = encode_varint(s.qpack_max_table_capacity, std::span<std::byte>{buf, 8});
    payload.insert(payload.end(), buf, buf + val2.bytes_written);

    // qpack_blocked_streams
    auto id3 = encode_varint(static_cast<std::uint64_t>(setting_id::qpack_blocked_streams), std::span<std::byte>{buf, 8});
    payload.insert(payload.end(), buf, buf + id3.bytes_written);
    auto val3 = encode_varint(s.qpack_blocked_streams, std::span<std::byte>{buf, 8});
    payload.insert(payload.end(), buf, buf + val3.bytes_written);

    // Encode payload length
    std::byte length_buf[8]{};
    auto length_result = encode_varint(payload.size(), std::span<std::byte>{length_buf, 8});
    output.insert(output.end(), length_buf, length_buf + length_result.bytes_written);

    // Append payload
    output.insert(output.end(), payload.begin(), payload.end());
    return flowq::buffer{output};
}

} // namespace flowq::quic::http3
