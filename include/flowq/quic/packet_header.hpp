#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>
#include <flowq/quic/varint.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace flowq::quic {

enum class packet_header_kind {
    version_negotiation,
    initial,
    handshake,
    retry,
    short_packet,
    structural_application
};

struct connection_id {
    flowq::buffer bytes;
};

struct version_negotiation_header {
    std::byte first_byte{};
    connection_id destination_connection_id;
    connection_id source_connection_id;
    std::vector<std::uint32_t> supported_versions;
};

struct initial_header {
    std::byte first_byte{};
    std::uint32_t version{};
    connection_id destination_connection_id;
    connection_id source_connection_id;
    flowq::buffer token;
    std::uint64_t length{};
    flowq::buffer protected_payload;
};

struct handshake_header {
    std::byte first_byte{};
    std::uint32_t version{};
    connection_id destination_connection_id;
    connection_id source_connection_id;
    std::uint64_t length{};
    flowq::buffer protected_payload;
};

struct retry_header {
    std::byte first_byte{};
    std::uint32_t version{};
    connection_id destination_connection_id;
    connection_id source_connection_id;
    flowq::buffer opaque_retry_tail;
};

struct structural_application_header {
    std::byte first_byte{};
    connection_id destination_connection_id;
    std::uint64_t length{};
    flowq::buffer protected_payload;
};

struct short_header {
    std::byte first_byte{};
    connection_id destination_connection_id;
    bool fixed_bit{};
    bool spin_bit{};
    bool key_phase{};
    std::size_t packet_number_length{};
    std::uint64_t truncated_packet_number{};
    flowq::buffer protected_payload;
};

using packet_header = std::variant<version_negotiation_header, initial_header, handshake_header, retry_header, short_header, structural_application_header>;

struct packet_header_decode_result {
    packet_header header{};
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

struct packet_header_encode_result {
    flowq::buffer payload;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

struct packet_number_length_result {
    std::size_t bytes{};
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

struct packet_number_encode_result {
    std::size_t bytes_written{};
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

struct packet_number_decode_result {
    std::uint64_t packet_number{};
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

[[nodiscard]] inline flowq::error packet_number_error(const char* message) {
    return flowq::error{flowq::error_code::protocol_error, message};
}

[[nodiscard]] inline bool valid_packet_number_length(std::size_t length) noexcept {
    return length >= 1 && length <= 4;
}

[[nodiscard]] inline packet_number_length_result select_packet_number_length(
    std::uint64_t packet_number,
    std::uint64_t largest_acknowledged) {
    if (packet_number <= largest_acknowledged) {
        return {0, packet_number_error("packet number must be greater than largest acknowledged")};
    }

    const auto gap = packet_number - largest_acknowledged;
    for (std::size_t length = 1; length <= 4; ++length) {
        const auto window = std::uint64_t{1} << (length * 8U);
        if (window > gap * 2U) {
            return {length, {}};
        }
    }

    return {0, packet_number_error("packet number gap exceeds 4-byte encoding window")};
}

[[nodiscard]] inline packet_number_encode_result encode_packet_number(
    std::uint64_t packet_number,
    std::size_t length,
    std::span<std::byte> output) {
    if (!valid_packet_number_length(length)) {
        return {0, packet_number_error("packet number length must be 1 to 4 bytes")};
    }
    if (output.size() < length) {
        return {0, packet_number_error("destination buffer too small for packet number")};
    }

    for (std::size_t index = 0; index < length; ++index) {
        const auto shift = (length - 1U - index) * 8U;
        output[index] = static_cast<std::byte>((packet_number >> shift) & 0xffU);
    }

    return {length, {}};
}

template <std::ranges::contiguous_range Range>
    requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<Range>>, std::byte>
[[nodiscard]] auto encode_packet_number(std::uint64_t packet_number, std::size_t length, Range& output) {
    return encode_packet_number(packet_number, length, std::span<std::byte>{std::ranges::data(output), std::ranges::size(output)});
}

[[nodiscard]] inline packet_number_decode_result decode_packet_number(
    std::uint64_t truncated_packet_number,
    std::size_t length,
    std::uint64_t largest_received) {
    if (!valid_packet_number_length(length)) {
        return {0, packet_number_error("packet number length must be 1 to 4 bytes")};
    }

    const auto packet_number_window = std::uint64_t{1} << (length * 8U);
    if (truncated_packet_number >= packet_number_window) {
        return {0, packet_number_error("truncated packet number does not fit selected length")};
    }

    const auto expected_packet_number = largest_received + 1U;
    const auto packet_number_half_window = packet_number_window / 2U;
    const auto packet_number_mask = packet_number_window - 1U;
    auto candidate_packet_number = (expected_packet_number & ~packet_number_mask) | truncated_packet_number;

    if (candidate_packet_number + packet_number_half_window <= expected_packet_number && candidate_packet_number < max_varint - packet_number_window) {
        candidate_packet_number += packet_number_window;
    } else if (candidate_packet_number > expected_packet_number + packet_number_half_window && candidate_packet_number >= packet_number_window) {
        candidate_packet_number -= packet_number_window;
    }

    return {candidate_packet_number, {}};
}

namespace detail {

[[nodiscard]] inline flowq::error packet_error(const char* message) {
    return flowq::error{flowq::error_code::protocol_error, message};
}

[[nodiscard]] inline bool read_u32(std::span<const std::byte> input, std::size_t& offset, std::uint32_t& value) {
    if (input.size() - offset < 4) {
        return false;
    }

    value = (static_cast<std::uint32_t>(input[offset]) << 24U) |
        (static_cast<std::uint32_t>(input[offset + 1]) << 16U) |
        (static_cast<std::uint32_t>(input[offset + 2]) << 8U) |
        static_cast<std::uint32_t>(input[offset + 3]);
    offset += 4;
    return true;
}

inline void append_u32(std::vector<std::byte>& output, std::uint32_t value) {
    output.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
    output.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
    output.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    output.push_back(static_cast<std::byte>(value & 0xffU));
}

[[nodiscard]] inline bool read_connection_id(std::span<const std::byte> input, std::size_t& offset, connection_id& id) {
    if (offset >= input.size()) {
        return false;
    }

    const auto length = static_cast<std::size_t>(input[offset]);
    ++offset;
    if (length > input.size() - offset) {
        return false;
    }

    id.bytes = flowq::buffer{input.subspan(offset, length)};
    offset += length;
    return true;
}

[[nodiscard]] inline bool append_connection_id(std::vector<std::byte>& output, const connection_id& id) {
    if (id.bytes.size() > 255) {
        return false;
    }

    output.push_back(static_cast<std::byte>(id.bytes.size()));
    output.insert(output.end(), id.bytes.data(), id.bytes.data() + static_cast<std::ptrdiff_t>(id.bytes.size()));
    return true;
}

[[nodiscard]] inline bool append_varint_to_packet(std::vector<std::byte>& output, std::uint64_t value) {
    std::array<std::byte, 8> encoded{};
    const auto result = encode_varint(value, encoded);
    if (!result.ok()) {
        return false;
    }

    output.insert(output.end(), encoded.begin(), encoded.begin() + static_cast<std::ptrdiff_t>(result.bytes_written));
    return true;
}

[[nodiscard]] inline packet_number_decode_result read_truncated_packet_number(
    std::span<const std::byte> input,
    std::size_t offset,
    std::size_t length) {
    if (!valid_packet_number_length(length)) {
        return {0, packet_number_error("packet number length must be 1 to 4 bytes")};
    }
    if (input.size() - offset < length) {
        return {0, packet_error("truncated short-header packet number")};
    }

    std::uint64_t value = 0;
    for (std::size_t index = 0; index < length; ++index) {
        value = (value << 8U) | static_cast<std::uint64_t>(input[offset + index]);
    }
    return {value, {}};
}

[[nodiscard]] inline std::byte short_header_first_byte(const short_header& header) {
    auto first = static_cast<std::uint8_t>((header.packet_number_length - 1U) & 0x03U);
    if (header.fixed_bit) {
        first |= 0x40U;
    }
    if (header.spin_bit) {
        first |= 0x20U;
    }
    if (header.key_phase) {
        first |= 0x04U;
    }
    return static_cast<std::byte>(first);
}

[[nodiscard]] inline bool read_packet_varint_at(std::span<const std::byte> input, std::size_t& offset, std::uint64_t& value) {
    const auto result = decode_varint(input.subspan(offset));
    if (!result.ok()) {
        return false;
    }

    value = result.value;
    offset += result.bytes_read;
    return true;
}

[[nodiscard]] inline packet_header_decode_result decode_version_negotiation(
    std::byte first_byte,
    std::span<const std::byte> input,
    std::size_t offset) {
    connection_id destination_connection_id;
    connection_id source_connection_id;
    if (!read_connection_id(input, offset, destination_connection_id) || !read_connection_id(input, offset, source_connection_id)) {
        return {{}, packet_error("truncated Version Negotiation connection ID")};
    }

    const auto remaining = input.size() - offset;
    if (remaining == 0 || remaining % 4 != 0) {
        return {{}, packet_error("invalid Version Negotiation version list length")};
    }

    std::vector<std::uint32_t> supported_versions;
    supported_versions.reserve(remaining / 4);
    while (offset < input.size()) {
        std::uint32_t version = 0;
        if (!read_u32(input, offset, version)) {
            return {{}, packet_error("truncated Version Negotiation version")};
        }
        supported_versions.push_back(version);
    }

    return {version_negotiation_header{first_byte, std::move(destination_connection_id), std::move(source_connection_id), std::move(supported_versions)}, {}};
}

[[nodiscard]] inline packet_header_decode_result decode_initial(
    std::byte first_byte,
    std::uint32_t version,
    connection_id destination_connection_id,
    connection_id source_connection_id,
    std::span<const std::byte> input,
    std::size_t offset) {
    std::uint64_t token_length = 0;
    if (!read_packet_varint_at(input, offset, token_length)) {
        return {{}, packet_error("truncated Initial token length")};
    }
    if (token_length > input.size() - offset) {
        return {{}, packet_error("truncated Initial token")};
    }

    auto token = flowq::buffer{input.subspan(offset, static_cast<std::size_t>(token_length))};
    offset += static_cast<std::size_t>(token_length);

    std::uint64_t length = 0;
    if (!read_packet_varint_at(input, offset, length)) {
        return {{}, packet_error("truncated Initial length")};
    }
    if (length != input.size() - offset) {
        return {{}, packet_error("Initial protected payload length mismatch")};
    }

    auto protected_payload = flowq::buffer{input.subspan(offset, static_cast<std::size_t>(length))};
    return {initial_header{first_byte, version, std::move(destination_connection_id), std::move(source_connection_id), std::move(token), length, std::move(protected_payload)}, {}};
}

[[nodiscard]] inline packet_header_decode_result decode_handshake(
    std::byte first_byte,
    std::uint32_t version,
    connection_id destination_connection_id,
    connection_id source_connection_id,
    std::span<const std::byte> input,
    std::size_t offset) {
    std::uint64_t length = 0;
    if (!read_packet_varint_at(input, offset, length)) {
        return {{}, packet_error("truncated Handshake length")};
    }
    if (length != input.size() - offset) {
        return {{}, packet_error("Handshake protected payload length mismatch")};
    }

    auto protected_payload = flowq::buffer{input.subspan(offset, static_cast<std::size_t>(length))};
    return {handshake_header{first_byte, version, std::move(destination_connection_id), std::move(source_connection_id), length, std::move(protected_payload)}, {}};
}

[[nodiscard]] inline packet_header_decode_result decode_retry(
    std::byte first_byte,
    std::uint32_t version,
    connection_id destination_connection_id,
    connection_id source_connection_id,
    std::span<const std::byte> input,
    std::size_t offset) {
    if (offset == input.size()) {
        return {{}, packet_error("Retry packet has empty opaque tail")};
    }

    auto opaque_tail = flowq::buffer{input.subspan(offset)};
    return {retry_header{first_byte, version, std::move(destination_connection_id), std::move(source_connection_id), std::move(opaque_tail)}, {}};
}

[[nodiscard]] inline packet_header_encode_result encode_version_negotiation(const version_negotiation_header& header) {
    if (header.supported_versions.empty()) {
        return {{}, packet_error("Version Negotiation requires at least one version")};
    }

    std::vector<std::byte> output;
    output.push_back(header.first_byte);
    append_u32(output, 0);
    if (!append_connection_id(output, header.destination_connection_id) || !append_connection_id(output, header.source_connection_id)) {
        return {{}, packet_error("connection ID is too long")};
    }
    for (auto version : header.supported_versions) {
        append_u32(output, version);
    }
    return {flowq::buffer{output}, {}};
}

[[nodiscard]] inline packet_header_encode_result encode_initial(const initial_header& header) {
    if (header.length != header.protected_payload.size()) {
        return {{}, packet_error("Initial length must match protected payload size")};
    }

    std::vector<std::byte> output;
    output.push_back(header.first_byte);
    append_u32(output, header.version);
    if (!append_connection_id(output, header.destination_connection_id) || !append_connection_id(output, header.source_connection_id) ||
        !append_varint_to_packet(output, header.token.size())) {
        return {{}, packet_error("failed to encode Initial header")};
    }
    output.insert(output.end(), header.token.data(), header.token.data() + static_cast<std::ptrdiff_t>(header.token.size()));
    if (!append_varint_to_packet(output, header.length)) {
        return {{}, packet_error("failed to encode Initial length")};
    }
    output.insert(output.end(), header.protected_payload.data(), header.protected_payload.data() + static_cast<std::ptrdiff_t>(header.protected_payload.size()));
    return {flowq::buffer{output}, {}};
}

[[nodiscard]] inline packet_header_encode_result encode_handshake(const handshake_header& header) {
    if (header.length != header.protected_payload.size()) {
        return {{}, packet_error("Handshake length must match protected payload size")};
    }

    std::vector<std::byte> output;
    output.push_back(header.first_byte);
    append_u32(output, header.version);
    if (!append_connection_id(output, header.destination_connection_id) || !append_connection_id(output, header.source_connection_id) ||
        !append_varint_to_packet(output, header.length)) {
        return {{}, packet_error("failed to encode Handshake header")};
    }
    output.insert(output.end(), header.protected_payload.data(), header.protected_payload.data() + static_cast<std::ptrdiff_t>(header.protected_payload.size()));
    return {flowq::buffer{output}, {}};
}

[[nodiscard]] inline packet_header_encode_result encode_retry(const retry_header& header) {
    if (header.opaque_retry_tail.empty()) {
        return {{}, packet_error("Retry packet requires an opaque tail")};
    }

    std::vector<std::byte> output;
    output.push_back(header.first_byte);
    append_u32(output, header.version);
    if (!append_connection_id(output, header.destination_connection_id) || !append_connection_id(output, header.source_connection_id)) {
        return {{}, packet_error("connection ID is too long")};
    }
    output.insert(output.end(), header.opaque_retry_tail.data(), header.opaque_retry_tail.data() + static_cast<std::ptrdiff_t>(header.opaque_retry_tail.size()));
    return {flowq::buffer{output}, {}};
}

[[nodiscard]] inline packet_header_encode_result encode_structural_application(const structural_application_header& header) {
    if (header.length != header.protected_payload.size()) {
        return {{}, packet_error("structural Application length must match protected payload size")};
    }

    std::vector<std::byte> output;
    output.push_back(header.first_byte);
    if (!append_connection_id(output, header.destination_connection_id) || !append_varint_to_packet(output, header.length)) {
        return {{}, packet_error("failed to encode structural Application header")};
    }
    output.insert(output.end(), header.protected_payload.data(), header.protected_payload.data() + static_cast<std::ptrdiff_t>(header.protected_payload.size()));
    return {flowq::buffer{output}, {}};
}

[[nodiscard]] inline packet_header_encode_result encode_short(const short_header& header) {
    if (!header.fixed_bit) {
        return {{}, packet_error("short header fixed bit must be set")};
    }
    if (!valid_packet_number_length(header.packet_number_length)) {
        return {{}, packet_error("short header packet number length must be 1 to 4 bytes")};
    }

    std::array<std::byte, 4> packet_number_bytes{};
    auto encoded_packet_number = encode_packet_number(header.truncated_packet_number, header.packet_number_length, packet_number_bytes);
    if (!encoded_packet_number.ok()) {
        return {{}, encoded_packet_number.error};
    }

    std::vector<std::byte> output;
    output.reserve(1 + header.destination_connection_id.bytes.size() + header.packet_number_length + header.protected_payload.size());
    output.push_back(short_header_first_byte(header));
    output.insert(
        output.end(),
        header.destination_connection_id.bytes.data(),
        header.destination_connection_id.bytes.data() + static_cast<std::ptrdiff_t>(header.destination_connection_id.bytes.size()));
    output.insert(output.end(), packet_number_bytes.begin(), packet_number_bytes.begin() + static_cast<std::ptrdiff_t>(header.packet_number_length));
    output.insert(output.end(), header.protected_payload.data(), header.protected_payload.data() + static_cast<std::ptrdiff_t>(header.protected_payload.size()));
    return {flowq::buffer{output}, {}};
}

} // namespace detail

[[nodiscard]] inline packet_header_decode_result decode_packet_header(std::span<const std::byte> input) {
    if (input.empty()) {
        return {{}, detail::packet_error("empty packet header input")};
    }

    const auto first_byte = input[0];
    const auto first = static_cast<std::uint8_t>(first_byte);
    if ((first & 0x80U) == 0) {
        return {{}, detail::packet_error("short headers are unsupported in M2b")};
    }

    std::size_t offset = 1;
    std::uint32_t version = 0;
    if (!detail::read_u32(input, offset, version)) {
        return {{}, detail::packet_error("truncated packet version")};
    }

    if (version == 0) {
        return detail::decode_version_negotiation(first_byte, input, offset);
    }

    if ((first & 0x40U) == 0) {
        return {{}, detail::packet_error("long header fixed bit is not set")};
    }

    connection_id destination_connection_id;
    connection_id source_connection_id;
    if (!detail::read_connection_id(input, offset, destination_connection_id) || !detail::read_connection_id(input, offset, source_connection_id)) {
        return {{}, detail::packet_error("truncated long-header connection ID")};
    }

    switch (first & 0x30U) {
    case 0x00U:
        return detail::decode_initial(first_byte, version, std::move(destination_connection_id), std::move(source_connection_id), input, offset);
    case 0x20U:
        return detail::decode_handshake(first_byte, version, std::move(destination_connection_id), std::move(source_connection_id), input, offset);
    case 0x30U:
        return detail::decode_retry(first_byte, version, std::move(destination_connection_id), std::move(source_connection_id), input, offset);
    default:
        return {{}, detail::packet_error("unsupported long-header packet type")};
    }
}

[[nodiscard]] inline packet_header_decode_result decode_short_header(
    std::span<const std::byte> input,
    std::size_t destination_connection_id_length) {
    if (input.empty()) {
        return {{}, detail::packet_error("empty short-header input")};
    }

    const auto first_byte = input[0];
    const auto first = static_cast<std::uint8_t>(first_byte);
    if ((first & 0x80U) != 0) {
        return {{}, detail::packet_error("short header must not use long-header form")};
    }
    if ((first & 0x40U) == 0) {
        return {{}, detail::packet_error("short header fixed bit is not set")};
    }
    if (destination_connection_id_length > input.size() - 1U) {
        return {{}, detail::packet_error("truncated short-header destination connection ID")};
    }

    const auto packet_number_length = static_cast<std::size_t>(first & 0x03U) + 1U;
    const auto packet_number_offset = 1U + destination_connection_id_length;
    auto packet_number = detail::read_truncated_packet_number(input, packet_number_offset, packet_number_length);
    if (!packet_number.ok()) {
        return {{}, packet_number.error};
    }

    const auto payload_offset = packet_number_offset + packet_number_length;
    return {short_header{
        first_byte,
        connection_id{flowq::buffer{input.subspan(1, destination_connection_id_length)}},
        true,
        (first & 0x20U) != 0,
        (first & 0x04U) != 0,
        packet_number_length,
        packet_number.packet_number,
        flowq::buffer{input.subspan(payload_offset)}
    }, {}};
}

[[nodiscard]] inline packet_header_decode_result decode_short_header(
    const flowq::buffer& input,
    std::size_t destination_connection_id_length) {
    return decode_short_header(std::span<const std::byte>{input.data(), input.size()}, destination_connection_id_length);
}

template <std::ranges::contiguous_range Range>
    requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<Range>>, std::byte>
[[nodiscard]] auto decode_short_header(const Range& input, std::size_t destination_connection_id_length) {
    return decode_short_header(std::span<const std::byte>{std::ranges::data(input), std::ranges::size(input)}, destination_connection_id_length);
}

[[nodiscard]] inline packet_header_decode_result decode_packet_header(const flowq::buffer& input) {
    return decode_packet_header(std::span<const std::byte>{input.data(), input.size()});
}

template <std::ranges::contiguous_range Range>
    requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<Range>>, std::byte>
[[nodiscard]] auto decode_packet_header(const Range& input) {
    return decode_packet_header(std::span<const std::byte>{std::ranges::data(input), std::ranges::size(input)});
}

[[nodiscard]] inline packet_header_encode_result encode_packet_header(const packet_header& header) {
    return std::visit(
        [](const auto& concrete_header) -> packet_header_encode_result {
            using header_type = std::decay_t<decltype(concrete_header)>;
            if constexpr (std::is_same_v<header_type, version_negotiation_header>) {
                return detail::encode_version_negotiation(concrete_header);
            } else if constexpr (std::is_same_v<header_type, initial_header>) {
                return detail::encode_initial(concrete_header);
            } else if constexpr (std::is_same_v<header_type, handshake_header>) {
                return detail::encode_handshake(concrete_header);
            } else if constexpr (std::is_same_v<header_type, retry_header>) {
                return detail::encode_retry(concrete_header);
            } else if constexpr (std::is_same_v<header_type, short_header>) {
                return detail::encode_short(concrete_header);
            } else {
                return detail::encode_structural_application(concrete_header);
            }
        },
        header);
}

} // namespace flowq::quic
