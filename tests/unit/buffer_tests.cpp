#include <flowq/buffer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>

TEST_CASE("buffer owns copied bytes") {
    std::array<std::byte, 3> payload{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    const flowq::buffer buffer{payload};
    payload[0] = std::byte{0xff};

    REQUIRE(buffer.size() == 3);
    CHECK(buffer.data()[0] == std::byte{0x01});
    CHECK(buffer.data()[1] == std::byte{0x02});
    CHECK(buffer.data()[2] == std::byte{0x03});
}

TEST_CASE("buffer can be constructed empty") {
    const flowq::buffer buffer{};

    CHECK(buffer.empty());
    CHECK(buffer.size() == 0);
}
