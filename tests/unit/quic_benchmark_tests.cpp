#include <flowq/benchmark.hpp>
#include <flowq/quic/varint.hpp>
#include <flowq/quic/frame.hpp>
#include <flowq/quic/packet_header.hpp>
#include <flowq/quic/congestion.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("benchmark varint encode") {
    flowq::benchmark::benchmark_suite suite;
    
    suite.add("varint_encode_small", []() {
        std::byte buf[8]{};
        auto result = flowq::quic::encode_varint(42, std::span<std::byte>{buf, 8});
        (void)result;
    });
    
    suite.add("varint_encode_medium", []() {
        std::byte buf[8]{};
        auto result = flowq::quic::encode_varint(16384, std::span<std::byte>{buf, 8});
        (void)result;
    });
    
    suite.add("varint_encode_large", []() {
        std::byte buf[8]{};
        auto result = flowq::quic::encode_varint(1073741824, std::span<std::byte>{buf, 8});
        (void)result;
    });
    
    auto results = suite.run(10000);
    REQUIRE(results.size() == 3);
    
    // Verify benchmarks ran successfully
    for (const auto& result : results) {
        CHECK(result.iterations == 10000);
        CHECK(result.total_time.count() > 0);
        CHECK(result.avg_time_ns > 0);
        CHECK(result.ops_per_second > 0);
    }
}

TEST_CASE("benchmark congestion controller") {
    flowq::benchmark::benchmark_suite suite;
    
    suite.add("congestion_on_packet_sent", []() {
        flowq::quic::congestion_controller controller;
        for (int i = 0; i < 100; ++i) {
            controller.on_packet_sent(1200, true);
        }
    });
    
    suite.add("congestion_on_packet_acknowledged", []() {
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
    REQUIRE(results.size() == 3);
    
    for (const auto& result : results) {
        CHECK(result.iterations == 1000);
        CHECK(result.total_time.count() > 0);
    }
}

TEST_CASE("benchmark packet header encode") {
    flowq::benchmark::benchmark_suite suite;
    
    suite.add("encode_initial_header", []() {
        flowq::quic::initial_header header{};
        header.first_byte = std::byte{0xc0};
        header.version = 1;
        header.destination_connection_id = flowq::quic::connection_id{flowq::buffer{std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}}}};
        header.source_connection_id = flowq::quic::connection_id{flowq::buffer{std::vector<std::byte>{std::byte{0x03}, std::byte{0x04}}}};
        header.length = 100;
        header.protected_payload = flowq::buffer{std::vector<std::byte>(100, std::byte{0x00})};
        
        auto result = flowq::quic::encode_packet_header(flowq::quic::packet_header{header});
        (void)result;
    });
    
    auto results = suite.run(5000);
    REQUIRE(results.size() == 1);
    
    CHECK(results[0].iterations == 5000);
    CHECK(results[0].total_time.count() > 0);
    CHECK(results[0].ops_per_second > 0);
}

TEST_CASE("benchmark suite format results") {
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
    CHECK(formatted.find("Iterations:") != std::string::npos);
    CHECK(formatted.find("Ops/sec:") != std::string::npos);
}
