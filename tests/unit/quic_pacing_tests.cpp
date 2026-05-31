#include <catch2/catch_test_macros.hpp>
#include <flowq/quic/pacing.hpp>
#include <thread>

using namespace flowq::quic;

TEST_CASE("pacing_controller initial state") {
    pacing_controller pacing;
    CHECK(pacing.get_interval().count() == 0);
    CHECK(pacing.get_rate_bps() == 0.0);
}

TEST_CASE("pacing_controller initialize sets interval") {
    pacing_controller pacing;
    pacing.initialize(12000, std::chrono::milliseconds(10));
    
    CHECK(pacing.get_interval().count() > 0);
    CHECK(pacing.get_rate_bps() > 0.0);
}

TEST_CASE("pacing_controller can send below threshold") {
    pacing_controller pacing;
    pacing.initialize(12000, std::chrono::milliseconds(10));
    
    // Below half congestion window
    CHECK(pacing.can_send(0, 1200));
    CHECK(pacing.can_send(5000, 1200));
}

TEST_CASE("pacing_controller can send respects timer") {
    pacing_controller pacing;
    pacing.initialize(12000, std::chrono::milliseconds(10));
    
    // First send should be allowed
    CHECK(pacing.can_send(10000, 1200));
    
    // Record a send
    pacing.on_packet_sent(1200);
    
    // Immediately after, should be blocked (if interval > 0)
    if (pacing.get_interval().count() > 0) {
        CHECK_FALSE(pacing.can_send(10000, 1200));
    }
}

TEST_CASE("pacing_controller time until next send") {
    pacing_controller pacing;
    pacing.initialize(12000, std::chrono::milliseconds(10));
    
    pacing.on_packet_sent(1200);
    
    auto time_until = pacing.time_until_next_send();
    CHECK(time_until.count() >= 0);
}

TEST_CASE("pacing_controller update congestion window") {
    pacing_controller pacing;
    pacing.initialize(12000, std::chrono::milliseconds(10));
    
    auto initial_interval = pacing.get_interval();
    
    // Double congestion window
    pacing.set_congestion_window(24000);
    
    // Interval should change
    CHECK(pacing.get_interval() != initial_interval);
}

TEST_CASE("pacing_controller update smoothed rtt") {
    pacing_controller pacing;
    pacing.initialize(12000, std::chrono::milliseconds(10));
    
    auto initial_interval = pacing.get_interval();
    
    // Halve RTT
    pacing.set_smoothed_rtt(std::chrono::milliseconds(5));
    
    // Interval should change
    CHECK(pacing.get_interval() != initial_interval);
}

TEST_CASE("pacing_controller reset") {
    pacing_controller pacing;
    pacing.initialize(12000, std::chrono::milliseconds(10));
    pacing.on_packet_sent(1200);
    
    pacing.reset();
    
    CHECK(pacing.get_interval().count() == 0);
    CHECK(pacing.get_rate_bps() == 0.0);
}

TEST_CASE("burst_absorber allow small burst") {
    burst_absorber burst;
    
    CHECK(burst.allow_burst(5));
    CHECK(burst.allow_burst(10));
    CHECK_FALSE(burst.allow_burst(11));
}

TEST_CASE("burst_absorber custom max burst") {
    burst_absorber burst;
    
    CHECK(burst.allow_burst(5, 5));
    CHECK_FALSE(burst.allow_burst(6, 5));
}

TEST_CASE("burst_absorber track statistics") {
    burst_absorber burst;
    
    burst.on_burst(5);
    burst.on_burst(10);
    burst.on_burst(3);
    
    CHECK(burst.total_bursts() == 3);
    CHECK(burst.total_packets_burst() == 18);
    CHECK(burst.max_burst_seen() == 10);
}
