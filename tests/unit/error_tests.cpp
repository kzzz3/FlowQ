#include <flowq/error.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("error stores code and message without throwing") {
    const flowq::error err{flowq::error_code::timeout, "deadline expired"};

    CHECK(err.code() == flowq::error_code::timeout);
    CHECK(err.message() == "deadline expired");
    CHECK_FALSE(err.ok());
}

TEST_CASE("default error represents success") {
    const flowq::error err{};

    CHECK(err.code() == flowq::error_code::none);
    CHECK(err.message().empty());
    CHECK(err.ok());
}
