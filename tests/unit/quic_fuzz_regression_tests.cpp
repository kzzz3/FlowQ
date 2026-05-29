#include <flowq/quic/frame.hpp>
#include <flowq/quic/packet_header.hpp>

#include "../fuzz/quic_codec_fuzz_harness.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <initializer_list>
#include <span>
#include <variant>
#include <vector>

namespace {

std::vector<std::byte> bytes(std::initializer_list<unsigned int> values) {
    std::vector<std::byte> output;
    output.reserve(values.size());
    for (auto value : values) {
        output.push_back(static_cast<std::byte>(value & 0xffU));
    }
    return output;
}

std::span<const std::byte> view(const std::vector<std::byte>& input) {
    return std::span<const std::byte>{input.data(), input.size()};
}

const std::vector<std::vector<std::byte>>& frame_fuzz_corpus() {
    static const std::vector<std::vector<std::byte>> corpus{
        bytes({}),
        bytes({0x00, 0x00, 0x01}),
        bytes({0x1f}),
        bytes({0x02, 0x64, 0x00, 0x01, 0x04}),
        bytes({0x02, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00}),
        bytes({0x06, 0x00, 0x02, 0xaa}),
        bytes({0x0a, 0x01, 0x02, 0xaa}),
        bytes({0x0f, 0x01, 0x00}),
        bytes({0x1c, 0x0a, 0x01, 0x04, 0x78}),
        bytes({0x10, 0x40}),
        bytes({0x11, 0x04, 0x48, 0x00}),
        bytes({0x15, 0x08, 0x60, 0x00})
    };
    return corpus;
}

const std::vector<std::vector<std::byte>>& packet_header_fuzz_corpus() {
    static const std::vector<std::vector<std::byte>> corpus{
        bytes({}),
        bytes({0x40}),
        bytes({0x80, 0x00}),
        bytes({0x80, 0x00, 0x00, 0x00, 0x00, 0x02, 0xaa}),
        bytes({0x80, 0x00, 0x00, 0x00, 0x00, 0x02, 0xaa, 0xbb, 0x01, 0xcc, 0x00, 0x00, 0x00, 0x01}),
        bytes({0x80, 0x00, 0x00, 0x00, 0x00, 0x02, 0xaa, 0xbb, 0x01, 0xcc, 0x00, 0x00, 0x00, 0x01, 0x70, 0x9a, 0x50, 0xc4}),
        bytes({0xc0, 0x00, 0x00, 0x00, 0x01, 0x01, 0xaa, 0x01, 0xbb, 0x02, 0x11, 0x22, 0x03, 0x01, 0x02, 0x03}),
        bytes({0xc0, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x40}),
        bytes({0xe0, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x02, 0xab, 0xcd}),
        bytes({0xf0, 0x00, 0x00, 0x00, 0x01, 0x01, 0xaa, 0x01, 0xbb, 0xde, 0xad, 0xbe, 0xef}),
        bytes({0xf0, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00}),
        bytes({0x65, 0xaa, 0xbb, 0xcc, 0x12, 0x34, 0xde, 0xad}),
        bytes({0x00, 0x01})
    };
    return corpus;
}

} // namespace

TEST_CASE("frame fuzz corpus is decoded without throwing") {
    for (const auto& input : frame_fuzz_corpus()) {
        CHECK_NOTHROW((void)flowq::quic::decode_frames(view(input)));
        CHECK_NOTHROW(flowq::quic::test_fuzz::exercise_frame_codec_input(view(input)));
    }
}

TEST_CASE("decodable frame fuzz corpus entries round trip through the encoder") {
    for (const auto& input : frame_fuzz_corpus()) {
        auto decoded = flowq::quic::decode_frames(view(input));
        if (!decoded.ok()) {
            continue;
        }

        for (const auto& frame : decoded.frames) {
            auto encoded = std::visit(
                [](const auto& concrete_frame) {
                    return flowq::quic::encode_frame(concrete_frame);
                },
                frame);
            REQUIRE(encoded.ok());

            auto roundtrip = flowq::quic::decode_frames(encoded.payload);
            REQUIRE(roundtrip.ok());
            REQUIRE(roundtrip.frames.size() == 1);
        }
    }
}

TEST_CASE("packet header fuzz corpus is decoded without throwing") {
    for (const auto& input : packet_header_fuzz_corpus()) {
        CHECK_NOTHROW((void)flowq::quic::decode_packet_header(view(input)));
        CHECK_NOTHROW((void)flowq::quic::decode_short_header(view(input), input.empty() ? 0U : input.size() - 1U));
        CHECK_NOTHROW(flowq::quic::test_fuzz::exercise_packet_header_input(view(input)));
    }
}

TEST_CASE("decodable packet header fuzz corpus entries round trip through the encoder") {
    for (const auto& input : packet_header_fuzz_corpus()) {
        auto decoded = flowq::quic::decode_packet_header(view(input));
        if (decoded.ok()) {
            auto encoded = flowq::quic::encode_packet_header(decoded.header);
            REQUIRE(encoded.ok());

            auto roundtrip = flowq::quic::decode_packet_header(encoded.payload);
            REQUIRE(roundtrip.ok());
        }

        for (auto dcid_length : {0U, 1U, 3U}) {
            auto short_decoded = flowq::quic::decode_short_header(view(input), dcid_length);
            if (!short_decoded.ok()) {
                continue;
            }

            auto encoded = flowq::quic::encode_packet_header(short_decoded.header);
            REQUIRE(encoded.ok());

            auto short_roundtrip = flowq::quic::decode_short_header(encoded.payload, dcid_length);
            REQUIRE(short_roundtrip.ok());
            REQUIRE(std::holds_alternative<flowq::quic::short_header>(short_roundtrip.header));
        }
    }
}
