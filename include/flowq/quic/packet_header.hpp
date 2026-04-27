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

using packet_header = std::variant<version_negotiation_header, initial_header, handshake_header, retry_header, structural_application_header>;

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
            } else {
                return detail::encode_structural_application(concrete_header);
            }
        },
        header);
}

} // namespace flowq::quic
