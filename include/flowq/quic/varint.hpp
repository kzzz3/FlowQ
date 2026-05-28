#pragma once

#include <flowq/error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <type_traits>

namespace flowq::quic {

inline constexpr std::uint64_t max_varint = 4611686018427387903ULL;

struct varint_size_result {
    std::size_t value{};
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

struct varint_encode_result {
    std::size_t bytes_written{};
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

struct varint_decode_result {
    std::uint64_t value{};
    std::size_t bytes_read{};
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

[[nodiscard]] inline flowq::error codec_error(const char* message) {
    return flowq::error{flowq::error_code::protocol_error, message};
}

[[nodiscard]] constexpr inline varint_size_result encoded_size(std::uint64_t value) {
    if (value > max_varint) {
        return {0, codec_error("QUIC varint value exceeds 2^62 - 1")};
    }

    if (value <= 63) {
        return {1, {}};
    }
    if (value <= 16383) {
        return {2, {}};
    }
    if (value <= 1073741823) {
        return {4, {}};
    }
    return {8, {}};
}

[[nodiscard]] inline varint_encode_result encode_varint(std::uint64_t value, std::span<std::byte> output) {
    // Fast path for common 1-byte case (0-63)
    if (value < 64) {
        if (output.size() < 1) {
            return {0, codec_error("destination buffer too small for QUIC varint")};
        }
        output[0] = static_cast<std::byte>(value);
        return {1, {}};
    }

    // Fast path for 2-byte case (64-16383)
    if (value < 16384) {
        if (output.size() < 2) {
            return {0, codec_error("destination buffer too small for QUIC varint")};
        }
        output[0] = static_cast<std::byte>(0x40U | ((value >> 8U) & 0x3fU));
        output[1] = static_cast<std::byte>(value & 0xffU);
        return {2, {}};
    }

    const auto size = encoded_size(value);
    if (!size.ok()) {
        return {0, size.error};
    }

    if (output.size() < size.value) {
        return {0, codec_error("destination buffer too small for QUIC varint")};
    }

    switch (size.value) {
    case 4:
        output[0] = static_cast<std::byte>(0x80U | ((value >> 24U) & 0x3fU));
        output[1] = static_cast<std::byte>((value >> 16U) & 0xffU);
        output[2] = static_cast<std::byte>((value >> 8U) & 0xffU);
        output[3] = static_cast<std::byte>(value & 0xffU);
        break;
    default:
        output[0] = static_cast<std::byte>(0xc0U | ((value >> 56U) & 0x3fU));
        output[1] = static_cast<std::byte>((value >> 48U) & 0xffU);
        output[2] = static_cast<std::byte>((value >> 40U) & 0xffU);
        output[3] = static_cast<std::byte>((value >> 32U) & 0xffU);
        output[4] = static_cast<std::byte>((value >> 24U) & 0xffU);
        output[5] = static_cast<std::byte>((value >> 16U) & 0xffU);
        output[6] = static_cast<std::byte>((value >> 8U) & 0xffU);
        output[7] = static_cast<std::byte>(value & 0xffU);
        break;
    }

    return {size.value, {}};
}

template <std::ranges::contiguous_range Range>
    requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<Range>>, std::byte>
[[nodiscard]] auto encode_varint(std::uint64_t value, Range& output) {
    return encode_varint(value, std::span<std::byte>{std::ranges::data(output), std::ranges::size(output)});
}

[[nodiscard]] inline varint_decode_result decode_varint(std::span<const std::byte> input) {
    if (input.empty()) {
        return {0, 0, codec_error("empty QUIC varint input")};
    }

    const auto first = static_cast<std::uint8_t>(input[0]);
    const auto prefix = first >> 6U;

    // Fast path for 1-byte case (prefix 00)
    if (prefix == 0) {
        return {static_cast<std::uint64_t>(first & 0x3fU), 1, {}};
    }

    // Fast path for 2-byte case (prefix 01)
    if (prefix == 1) {
        if (input.size() < 2) {
            return {0, 0, codec_error("truncated QUIC varint")};
        }
        const auto value = static_cast<std::uint64_t>((first & 0x3fU) << 8U) |
                          static_cast<std::uint64_t>(static_cast<std::uint8_t>(input[1]));
        return {value, 2, {}};
    }

    const std::size_t length = prefix == 2 ? 4 : 8;

    if (input.size() < length) {
        return {0, 0, codec_error("truncated QUIC varint")};
    }

    std::uint64_t value = first & 0x3fU;
    for (std::size_t index = 1; index < length; ++index) {
        value = (value << 8U) | static_cast<std::uint8_t>(input[index]);
    }

    if (value > max_varint) {
        return {0, 0, codec_error("decoded QUIC varint exceeds 2^62 - 1")};
    }

    return {value, length, {}};
}

template <std::ranges::contiguous_range Range>
    requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<Range>>, std::byte>
[[nodiscard]] auto decode_varint(const Range& input) {
    return decode_varint(std::span<const std::byte>{std::ranges::data(input), std::ranges::size(input)});
}

} // namespace flowq::quic
