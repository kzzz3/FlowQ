#include <flowq/endpoint.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("endpoint stores host port and ALPN") {
    const flowq::endpoint endpoint{"example.com", 443, "h3"};

    CHECK(endpoint.host == "example.com");
    CHECK(endpoint.port == 443);
    CHECK(endpoint.alpn == "h3");
}
