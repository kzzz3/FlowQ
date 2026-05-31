#include <flowq/benchmark.hpp>
#include <flowq/quic/session.hpp>
#include <flowq/quic/connection.hpp>
#include <flowq/quic/packet_pipeline.hpp>
#include "plaintext_packet_protector.hpp"
#include <flowq/quic/qpack.hpp>
#include <flowq/quic/http3.hpp>
#include <flowq/quic/congestion.hpp>

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace {

flowq::quic::connection_id make_cid(std::initializer_list<unsigned char> bytes) {
    std::vector<std::byte> data;
    for (auto b : bytes) {
        data.push_back(static_cast<std::byte>(b));
    }
    return flowq::quic::connection_id{flowq::buffer{data}};
}

flowq::buffer make_text(const std::string& text) {
    std::vector<std::byte> bytes;
    for (char c : text) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
    return flowq::buffer{bytes};
}

} // namespace

TEST_CASE("benchmark session lifecycle") {
    flowq::benchmark::benchmark_suite suite;

    suite.add("session_create_destroy", []() {
        flowq::quic::test::plaintext_packet_protector_set protector{};
        flowq::quic::session_config config{
            .role = flowq::quic::connection_role::client,
            .local_connection_id = make_cid({0x01}),
            .remote_connection_id = make_cid({0x02}),
            .peer = flowq::endpoint{"server", 4433, "hq-interop"},
            .initial_tx_protector = &protector.initial,
            .initial_rx_protector = &protector.initial,
            .handshake_tx_protector = &protector.handshake,
            .handshake_rx_protector = &protector.handshake,
            .application_tx_protector = &protector.application,
            .application_rx_protector = &protector.application,
        };
        flowq::quic::session session{std::move(config)};
    });

    auto results = suite.run(1000);
    REQUIRE(results.size() == 1);
    CHECK(results[0].ops_per_second > 0);
}

TEST_CASE("benchmark packet encoding") {
    flowq::benchmark::benchmark_suite suite;

    suite.add("encode_ping_frame", []() {
        flowq::quic::ping_frame ping{};
        auto result = flowq::quic::encode_frame(ping);
        (void)result;
    });

    suite.add("encode_stream_frame", []() {
        flowq::quic::stream_frame stream{};
        stream.stream_id = 0;
        stream.data = make_text("Hello, World!");
        auto result = flowq::quic::encode_frame(stream);
        (void)result;
    });

    suite.add("encode_ack_frame", []() {
        flowq::quic::ack_frame ack{};
        ack.largest_acknowledged = 100;
        ack.first_ack_range = 10;
        auto result = flowq::quic::encode_frame(ack);
        (void)result;
    });

    auto results = suite.run(10000);
    REQUIRE(results.size() == 3);
    for (const auto& result : results) {
        CHECK(result.ops_per_second > 0);
    }
}

TEST_CASE("benchmark packet header encoding") {
    flowq::benchmark::benchmark_suite suite;

    suite.add("encode_initial_header", []() {
        flowq::quic::initial_header header{};
        header.first_byte = std::byte{0xc0};
        header.version = 1;
        header.destination_connection_id = make_cid({0x01, 0x02});
        header.source_connection_id = make_cid({0x03, 0x04});
        header.length = 100;
        header.protected_payload = flowq::buffer{std::vector<std::byte>(100, std::byte{0x00})};
        auto result = flowq::quic::encode_packet_header(flowq::quic::packet_header{header});
        (void)result;
    });

    auto results = suite.run(5000);
    REQUIRE(results.size() == 1);
    CHECK(results[0].ops_per_second > 0);
}

TEST_CASE("benchmark QPACK encoding") {
    flowq::benchmark::benchmark_suite suite;

    suite.add("qpack_encode_static_headers", []() {
        flowq::quic::qpack::encoder encoder;
        std::vector<flowq::quic::qpack::header_field> headers = {
            {":method", "GET"},
            {":path", "/index.html"},
            {":authority", "example.com"},
            {":scheme", "https"}
        };
        auto result = encoder.encode(headers);
        (void)result;
    });

    suite.add("qpack_encode_literal_headers", []() {
        flowq::quic::qpack::encoder encoder;
        std::vector<flowq::quic::qpack::header_field> headers = {
            {"x-custom-header", "custom-value"},
            {"x-request-id", "abc123"}
        };
        auto result = encoder.encode(headers);
        (void)result;
    });

    auto results = suite.run(5000);
    REQUIRE(results.size() == 2);
    for (const auto& result : results) {
        CHECK(result.ops_per_second > 0);
    }
}

TEST_CASE("benchmark HTTP/3 frame encoding") {
    flowq::benchmark::benchmark_suite suite;

    suite.add("http3_encode_data_frame", []() {
        flowq::buffer payload = make_text("Hello, World!");
        auto result = flowq::quic::http3::encode_data_frame(payload);
        (void)result;
    });

    suite.add("http3_encode_settings_frame", []() {
        flowq::quic::http3::settings settings{};
        auto result = flowq::quic::http3::encode_settings_frame(settings);
        (void)result;
    });

    suite.add("http3_encode_goaway_frame", []() {
        auto result = flowq::quic::http3::encode_goaway_frame(42);
        (void)result;
    });

    auto results = suite.run(10000);
    REQUIRE(results.size() == 3);
    for (const auto& result : results) {
        CHECK(result.ops_per_second > 0);
    }
}

TEST_CASE("benchmark comprehensive congestion controller") {
    flowq::benchmark::benchmark_suite suite;

    suite.add("congestion_send_ack_cycle", []() {
        flowq::quic::congestion_controller controller;
        for (int i = 0; i < 100; ++i) {
            controller.on_packet_sent(1200, true);
        }
        for (int i = 0; i < 100; ++i) {
            controller.on_packet_acknowledged(1200);
        }
    });

    suite.add("congestion_can_send_check", []() {
        flowq::quic::congestion_controller controller;
        for (int i = 0; i < 1000; ++i) {
            (void)controller.can_send();
        }
    });

    auto results = suite.run(1000);
    REQUIRE(results.size() == 2);
    for (const auto& result : results) {
        CHECK(result.ops_per_second > 0);
    }
}

TEST_CASE("benchmark format results") {
    flowq::benchmark::benchmark_suite suite;

    suite.add("integer_accumulation", []() {
        volatile int x = 0;
        for (int i = 0; i < 100; ++i) {
            x += i;
        }
        (void)x;
    });

    auto results = suite.run(100);
    auto formatted = flowq::benchmark::format_results(results);

    CHECK_FALSE(formatted.empty());
    CHECK(formatted.find("integer_accumulation") != std::string::npos);
    CHECK(formatted.find("Ops/sec:") != std::string::npos);
}
