#include <flowq/quic/http3_server.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("http3_server constructs with config") {
    flowq::quic::http3::server_config config{};
    config.host = "localhost";
    config.port = 8080;
    config.max_connections = 100;
    config.max_streams_bidi = 10;
    config.max_streams_uni = 10;

    flowq::quic::http3::server server{config};

    CHECK(server.config().host == "localhost");
    CHECK(server.config().port == 8080);
    CHECK(server.config().max_connections == 100);
    CHECK_FALSE(server.running());
    CHECK(server.handler_count() == 0);
}

TEST_CASE("http3_server starts and stops") {
    flowq::quic::http3::server_config config{};
    config.host = "localhost";
    config.port = 8080;

    flowq::quic::http3::server server{config};

    auto start_result = server.start();
    REQUIRE(start_result.ok());
    CHECK(server.running());

    auto stop_result = server.stop();
    REQUIRE(stop_result.ok());
    CHECK_FALSE(server.running());
}

TEST_CASE("http3_server rejects double start") {
    flowq::quic::http3::server_config config{};
    config.host = "localhost";
    config.port = 8080;

    flowq::quic::http3::server server{config};
    server.start();

    auto result = server.start();
    CHECK_FALSE(result.ok());
}

TEST_CASE("http3_server rejects stop when not running") {
    flowq::quic::http3::server_config config{};
    config.host = "localhost";
    config.port = 8080;

    flowq::quic::http3::server server{config};

    auto result = server.stop();
    CHECK_FALSE(result.ok());
}

TEST_CASE("http3_server registers request handler") {
    flowq::quic::http3::server_config config{};
    config.host = "localhost";
    config.port = 8080;

    flowq::quic::http3::server server{config};

    server.on_request("/api/data", [](const flowq::quic::http3::server_request& req) {
        flowq::quic::http3::server_response response{};
        response.status_code = 200;
        response.headers["content-type"] = "application/json";
        response.body = flowq::buffer{std::vector<std::byte>{std::byte{0x7b}, std::byte{0x7d}}};
        return response;
    });

    CHECK(server.handler_count() == 1);
}

TEST_CASE("http3_server handles registered request") {
    flowq::quic::http3::server_config config{};
    config.host = "localhost";
    config.port = 8080;

    flowq::quic::http3::server server{config};

    server.on_request("/api/data", [](const flowq::quic::http3::server_request& req) {
        flowq::quic::http3::server_response response{};
        response.status_code = 200;
        response.headers["content-type"] = "application/json";
        response.body = flowq::buffer{std::vector<std::byte>{std::byte{0x7b}, std::byte{0x7d}}};
        return response;
    });

    flowq::quic::http3::server_request request{};
    request.method = "GET";
    request.path = "/api/data";
    request.authority = "example.com";
    request.scheme = "https";

    auto response = server.handle_request(request);
    CHECK(response.status_code == 200);
    CHECK(response.headers.at("content-type") == "application/json");
}

TEST_CASE("http3_server returns 404 for unregistered path") {
    flowq::quic::http3::server_config config{};
    config.host = "localhost";
    config.port = 8080;

    flowq::quic::http3::server server{config};

    flowq::quic::http3::server_request request{};
    request.method = "GET";
    request.path = "/nonexistent";
    request.authority = "example.com";
    request.scheme = "https";

    auto response = server.handle_request(request);
    CHECK(response.status_code == 404);
}

TEST_CASE("http3_server_builder creates valid server") {
    auto server = flowq::quic::http3::server_builder{}
        .host("localhost")
        .port(8080)
        .max_connections(50)
        .max_streams_bidi(5)
        .max_streams_uni(5)
        .build();

    CHECK(server.config().host == "localhost");
    CHECK(server.config().port == 8080);
    CHECK(server.config().max_connections == 50);
    CHECK(server.config().max_streams_bidi == 5);
    CHECK(server.config().max_streams_uni == 5);
}

TEST_CASE("http3_server handles multiple routes") {
    flowq::quic::http3::server_config config{};
    config.host = "localhost";
    config.port = 8080;

    flowq::quic::http3::server server{config};

    server.on_request("/", [](const flowq::quic::http3::server_request& req) {
        flowq::quic::http3::server_response response{};
        response.status_code = 200;
        return response;
    });

    server.on_request("/api/data", [](const flowq::quic::http3::server_request& req) {
        flowq::quic::http3::server_response response{};
        response.status_code = 200;
        return response;
    });

    server.on_request("/api/users", [](const flowq::quic::http3::server_request& req) {
        flowq::quic::http3::server_response response{};
        response.status_code = 200;
        return response;
    });

    CHECK(server.handler_count() == 3);

    flowq::quic::http3::server_request request{};
    request.method = "GET";
    request.path = "/api/data";
    request.authority = "example.com";
    request.scheme = "https";

    auto response = server.handle_request(request);
    CHECK(response.status_code == 200);
}

TEST_CASE("http3_server config has correct defaults") {
    flowq::quic::http3::server_config config{};

    CHECK(config.host.empty());
    CHECK(config.port == 0);
    CHECK(config.max_connections == 100);
    CHECK(config.max_streams_bidi == 10);
    CHECK(config.max_streams_uni == 10);
}
