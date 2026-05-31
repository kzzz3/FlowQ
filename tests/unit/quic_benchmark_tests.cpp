#include <flowq/benchmark.hpp>
#include <flowq/quic/varint.hpp>
#include <flowq/quic/frame.hpp>
#include <flowq/quic/packet_header.hpp>
#include <flowq/quic/congestion.hpp>
#include <flowq/quic/ack_loss.hpp>
#include <flowq/quic/key_update.hpp>
#include <flowq/quic/initial_keys.hpp>
#include <flowq/buffer.hpp>

#include <catch2/catch_test_macros.hpp>
#include <random>

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

// ============================================================================
// Performance Benchmarks
// ============================================================================

TEST_CASE("benchmark buffer operations") {
    flowq::benchmark::benchmark_suite suite;

    suite.add("buffer_create_small", []() {
        flowq::buffer buf(1024);
        (void)buf;
    });

    suite.add("buffer_append_1kb", []() {
        flowq::buffer buf;
        std::vector<std::byte> data(1024, std::byte{0x42});
        buf.append(std::span<const std::byte>{data});
    });

    suite.add("buffer_secure_zero_1kb", []() {
        flowq::buffer buf(1024);
        buf.resize(1024, std::byte{0x42});
        buf.secure_zero();
    });

    auto results = suite.run(10000);
    REQUIRE(results.size() == 3);

    for (const auto& result : results) {
        CHECK(result.iterations == 10000);
        CHECK(result.total_time.count() > 0);
        CHECK(result.ops_per_second > 0);
    }
}

TEST_CASE("benchmark RTT estimation") {
    flowq::benchmark::benchmark_suite suite;

    suite.add("rtt_update_smooth", []() {
        flowq::quic::rtt_estimator rtt;
        flowq::quic::rtt_sample sample{};
        sample.latest_rtt = std::chrono::milliseconds(10);
        sample.ack_delay = std::chrono::milliseconds(0);
        sample.peer_max_ack_delay = std::chrono::milliseconds(25);
        for (int i = 0; i < 100; ++i) {
            rtt.update(sample);
        }
    });

    suite.add("rtt_update_varying", []() {
        flowq::quic::rtt_estimator rtt;
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> dist(5, 50);
        for (int i = 0; i < 100; ++i) {
            flowq::quic::rtt_sample sample{};
            sample.latest_rtt = std::chrono::milliseconds(dist(rng));
            sample.ack_delay = std::chrono::milliseconds(0);
            sample.peer_max_ack_delay = std::chrono::milliseconds(25);
            rtt.update(sample);
        }
    });

    suite.add("rtt_smoothed_value", []() {
        flowq::quic::rtt_estimator rtt;
        flowq::quic::rtt_sample sample{};
        sample.latest_rtt = std::chrono::milliseconds(10);
        sample.ack_delay = std::chrono::milliseconds(0);
        sample.peer_max_ack_delay = std::chrono::milliseconds(25);
        rtt.update(sample);
        for (int i = 0; i < 1000; ++i) {
            volatile auto smoothed = rtt.smoothed_rtt();
            (void)smoothed;
        }
    });

    auto results = suite.run(1000);
    REQUIRE(results.size() == 3);

    for (const auto& result : results) {
        CHECK(result.iterations == 1000);
        CHECK(result.total_time.count() > 0);
    }
}

TEST_CASE("benchmark key update state") {
    flowq::benchmark::benchmark_suite suite;

    suite.add("key_update_receive_phase", []() {
        flowq::quic::key_update_state state;
        for (int i = 0; i < 1000; ++i) {
            state.receive_key_phase(flowq::quic::key_phase::phase_0);
        }
    });

    suite.add("key_update_initiate", []() {
        flowq::quic::key_update_state state;
        state.receive_key_phase(flowq::quic::key_phase::phase_0);
        state.initiate_update();
        state.complete_update();
        state.receive_key_phase(flowq::quic::key_phase::phase_1);
    });

    auto results = suite.run(10000);
    REQUIRE(results.size() == 2);

    for (const auto& result : results) {
        CHECK(result.iterations == 10000);
        CHECK(result.total_time.count() > 0);
    }
}

// ============================================================================
// Loss Recovery Benchmarks
// ============================================================================

TEST_CASE("benchmark sent packet tracker") {
    flowq::benchmark::benchmark_suite suite;

    suite.add("tracker_on_packet_sent", []() {
        flowq::quic::sent_packet_tracker tracker(flowq::quic::packet_number_space::application);
        for (std::uint64_t i = 0; i < 100; ++i) {
            tracker.on_packet_sent(i, true);
        }
    });

    suite.add("tracker_on_ack_received", []() {
        flowq::quic::sent_packet_tracker tracker(flowq::quic::packet_number_space::application);
        for (std::uint64_t i = 0; i < 100; ++i) {
            tracker.on_packet_sent(i, true);
        }
        flowq::quic::ack_frame ack{};
        ack.largest_acknowledged = 99;
        ack.ack_delay = 0;
        ack.ranges.push_back(flowq::quic::ack_range{0, 99});
        tracker.on_ack_received(ack);
    });

    auto results = suite.run(1000);
    REQUIRE(results.size() == 2);

    for (const auto& result : results) {
        CHECK(result.iterations == 1000);
        CHECK(result.total_time.count() > 0);
    }
}

// ============================================================================
// Connection Migration Benchmarks
// ============================================================================

TEST_CASE("benchmark path validation") {
    flowq::benchmark::benchmark_suite suite;

    suite.add("path_challenge_token", []() {
        // Benchmark path challenge token generation
        std::array<std::byte, 8> token{};
        for (int i = 0; i < 100; ++i) {
            // Simulate token generation
            for (auto& b : token) {
                b = std::byte{static_cast<unsigned char>(i)};
            }
        }
    });

    auto results = suite.run(10000);
    REQUIRE(results.size() == 1);

    CHECK(results[0].iterations == 10000);
    CHECK(results[0].total_time.count() > 0);
}
