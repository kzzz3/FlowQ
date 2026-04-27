#include <flowq/quic/varint.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

TEST_CASE("QUIC varint encoded size follows RFC 9000 boundaries") {
    CHECK(flowq::quic::encoded_size(0).value == 1);
    CHECK(flowq::quic::encoded_size(63).value == 1);
    CHECK(flowq::quic::encoded_size(64).value == 2);
    CHECK(flowq::quic::encoded_size(16383).value == 2);
    CHECK(flowq::quic::encoded_size(16384).value == 4);
    CHECK(flowq::quic::encoded_size(1073741823).value == 4);
    CHECK(flowq::quic::encoded_size(1073741824).value == 8);
    CHECK(flowq::quic::encoded_size(flowq::quic::max_varint).value == 8);
    CHECK_FALSE(flowq::quic::encoded_size(flowq::quic::max_varint + 1).ok());
}

TEST_CASE("QUIC varint encodes minimally and decodes boundary values") {
    std::array<std::uint64_t, 8> values{
        0,
        63,
        64,
        16383,
        16384,
        1073741823,
        1073741824,
        flowq::quic::max_varint
    };

    for (auto value : values) {
        std::array<std::byte, 8> output{};
        auto encoded = flowq::quic::encode_varint(value, output);
        REQUIRE(encoded.ok());

        auto decoded = flowq::quic::decode_varint(std::span<const std::byte>{output.data(), encoded.bytes_written});
        REQUIRE(decoded.ok());
        CHECK(decoded.value == value);
        CHECK(decoded.bytes_read == encoded.bytes_written);
    }
}

TEST_CASE("QUIC varint uses exact RFC 9000 minimal byte encodings") {
    struct case_row {
        std::uint64_t value;
        std::array<std::byte, 8> bytes;
        std::size_t size;
    };

    std::array<case_row, 8> cases{{
        {0, {std::byte{0x00}}, 1},
        {63, {std::byte{0x3f}}, 1},
        {64, {std::byte{0x40}, std::byte{0x40}}, 2},
        {16383, {std::byte{0x7f}, std::byte{0xff}}, 2},
        {16384, {std::byte{0x80}, std::byte{0x00}, std::byte{0x40}, std::byte{0x00}}, 4},
        {1073741823, {std::byte{0xbf}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}}, 4},
        {1073741824, {std::byte{0xc0}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x40}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}}, 8},
        {flowq::quic::max_varint, {std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}}, 8}
    }};

    for (const auto& test_case : cases) {
        std::array<std::byte, 8> output{};
        auto encoded = flowq::quic::encode_varint(test_case.value, output);
        REQUIRE(encoded.ok());
        REQUIRE(encoded.bytes_written == test_case.size);
        CHECK(std::equal(output.begin(), output.begin() + static_cast<std::ptrdiff_t>(encoded.bytes_written), test_case.bytes.begin()));
    }
}

TEST_CASE("QUIC varint decoder accepts valid non-minimal encodings") {
    std::array<std::byte, 2> encoded{std::byte{0x40}, std::byte{0x25}};

    auto decoded = flowq::quic::decode_varint(encoded);

    REQUIRE(decoded.ok());
    CHECK(decoded.value == 37);
    CHECK(decoded.bytes_read == 2);
}

TEST_CASE("QUIC varint reports truncated and oversized encodings") {
    CHECK_FALSE(flowq::quic::decode_varint(std::span<const std::byte>{}).ok());

    std::array<std::byte, 1> truncated_two{std::byte{0x40}};
    CHECK_FALSE(flowq::quic::decode_varint(truncated_two).ok());

    std::array<std::byte, 3> truncated_four{std::byte{0x80}, std::byte{0x00}, std::byte{0x00}};
    CHECK_FALSE(flowq::quic::decode_varint(truncated_four).ok());

    std::array<std::byte, 7> truncated_eight{
        std::byte{0xc0}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}
    };
    CHECK_FALSE(flowq::quic::decode_varint(truncated_eight).ok());

    std::array<std::byte, 1> too_small{};
    CHECK_FALSE(flowq::quic::encode_varint(64, too_small).ok());
}
