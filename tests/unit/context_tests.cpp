#include <flowq/context.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("context exposes its ASIO io_context") {
    flowq::context context{};

    CHECK(&context.io_context() == &context.io_context());
}
