#pragma once

#include <flowq/quic/frame.hpp>
#include <flowq/quic/packet_header.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

namespace flowq::quic::test_fuzz {

inline void exercise_frame_codec_input(std::span<const std::byte> input) {
    auto result = decode_frames(input);
    if (!result.ok()) {
        return;
    }

    for (const auto& frame : result.frames) {
        auto encoded = std::visit(
            [](const auto& concrete_frame) {
                return encode_frame(concrete_frame);
            },
            frame);
        if (encoded.ok()) {
            auto roundtrip = decode_frames(encoded.payload);
            (void)roundtrip;
        }
    }
}

inline void exercise_packet_header_input(std::span<const std::byte> input) {
    auto result = decode_packet_header(input);
    if (result.ok()) {
        auto encoded = encode_packet_header(result.header);
        if (encoded.ok()) {
            auto roundtrip = decode_packet_header(encoded.payload);
            (void)roundtrip;
        }
    }

    if (input.size() <= 1U) {
        return;
    }

    const auto first = static_cast<std::uint8_t>(input[0]);
    const auto destination_connection_id_length = static_cast<std::size_t>(first % input.size());
    auto short_result = decode_short_header(input, destination_connection_id_length);
    if (!short_result.ok()) {
        return;
    }

    auto encoded = encode_packet_header(short_result.header);
    if (encoded.ok()) {
        auto short_roundtrip = decode_short_header(encoded.payload, destination_connection_id_length);
        (void)short_roundtrip;
    }
}

} // namespace flowq::quic::test_fuzz
