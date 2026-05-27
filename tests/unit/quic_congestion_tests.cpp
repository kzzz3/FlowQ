#include <flowq/quic/congestion.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>

using namespace std::chrono_literals;

namespace {

using clock_type = std::chrono::steady_clock;

clock_type::time_point at(std::chrono::milliseconds offset) {
    return clock_type::time_point{offset};
}

} // namespace

TEST_CASE("congestion controller tracks bytes-in-flight on ack-eliciting send") {
    flowq::quic::congestion_controller controller{};

    controller.on_packet_sent(1200, true);

    CHECK(controller.bytes_in_flight() == 1200);
}

TEST_CASE("congestion controller ignores non-ack-eliciting send for bytes-in-flight") {
    flowq::quic::congestion_controller controller{};

    controller.on_packet_sent(1200, false);

    CHECK(controller.bytes_in_flight() == 0);
}

TEST_CASE("congestion controller decreases bytes-in-flight on acknowledgment") {
    flowq::quic::congestion_controller controller{};
    controller.on_packet_sent(1200, true);
    controller.on_packet_sent(800, true);

    controller.on_packet_acknowledged(1200);

    CHECK(controller.bytes_in_flight() == 800);
}

TEST_CASE("congestion controller decreases bytes-in-flight on loss") {
    flowq::quic::congestion_controller controller{};
    controller.on_packet_sent(1200, true);
    controller.on_packet_sent(800, true);

    controller.on_packet_lost(1200);

    CHECK(controller.bytes_in_flight() == 800);
}

TEST_CASE("congestion controller starts in slow start") {
    flowq::quic::congestion_controller controller{};

    CHECK(controller.state() == flowq::quic::congestion_phase::slow_start);
    CHECK(controller.congestion_window() == flowq::quic::default_initial_window());
}

TEST_CASE("congestion controller grows window in slow start by acknowledged bytes") {
    flowq::quic::congestion_controller controller{};
    controller.on_packet_sent(1200, true);

    controller.on_packet_acknowledged(1200);

    CHECK(controller.congestion_window() == flowq::quic::default_initial_window() + 1200);
}

TEST_CASE("congestion controller transitions to congestion avoidance when window exceeds slow start threshold") {
    flowq::quic::congestion_controller controller{};
    const auto ssthresh = flowq::quic::default_initial_window();
    controller.set_slow_start_threshold(ssthresh);

    controller.on_packet_sent(1200, true);
    controller.on_packet_acknowledged(1200);

    CHECK(controller.state() == flowq::quic::congestion_phase::congestion_avoidance);
}

TEST_CASE("congestion controller grows window linearly in congestion avoidance") {
    flowq::quic::congestion_controller controller{};
    controller.set_slow_start_threshold(flowq::quic::default_initial_window());
    controller.enter_congestion_avoidance();
    const auto window_before = controller.congestion_window();

    controller.on_packet_sent(1200, true);
    controller.on_packet_acknowledged(1200);

    // NewReno: cwnd += MSS * acked / cwnd (linear growth)
    CHECK(controller.congestion_window() > window_before);
    CHECK(controller.congestion_window() < window_before + 1200);
}

TEST_CASE("congestion controller halves window on loss event") {
    flowq::quic::congestion_controller controller{};
    const auto initial_window = controller.congestion_window();

    controller.on_congestion_event();

    CHECK(controller.congestion_window() == initial_window / 2);
    CHECK(controller.slow_start_threshold() == initial_window / 2);
}

TEST_CASE("congestion controller enters congestion avoidance after loss event") {
    flowq::quic::congestion_controller controller{};

    controller.on_congestion_event();

    CHECK(controller.state() == flowq::quic::congestion_phase::congestion_avoidance);
}

TEST_CASE("congestion controller enforces minimum window on repeated congestion events") {
    flowq::quic::congestion_controller controller{};
    const auto minimum = flowq::quic::default_minimum_window();

    for (int i = 0; i < 20; ++i) {
        controller.on_congestion_event();
        CHECK(controller.congestion_window() >= minimum);
    }

    CHECK(controller.congestion_window() == minimum);
    CHECK(controller.slow_start_threshold() == minimum);
}

TEST_CASE("congestion controller returns to slow start when window grows back past threshold") {
    flowq::quic::congestion_controller controller{};
    const auto initial_window = flowq::quic::default_initial_window();
    controller.set_slow_start_threshold(initial_window * 2);

    // Should stay in slow start since cwnd < ssthresh
    CHECK(controller.state() == flowq::quic::congestion_phase::slow_start);
}

TEST_CASE("congestion controller detects persistent congestion on long quiescent period") {
    flowq::quic::congestion_controller controller{};
    controller.update_rtt(flowq::quic::rtt_sample{100ms, 0ms, 25ms, true});

    // Send two packets, lose both, long gap between
    controller.on_packet_sent(1200, true);
    controller.on_packet_lost(1200);

    bool persistent = controller.detect_persistent_congestion(
        std::vector<flowq::quic::congestion_packet>{
            {0, at(0ms), true, 1200},
            {1, at(400ms), true, 1200}
        },
        at(2000ms));

    CHECK(persistent);
}

TEST_CASE("congestion controller does not detect persistent congestion with short gap") {
    flowq::quic::congestion_controller controller{};
    controller.update_rtt(flowq::quic::rtt_sample{100ms, 0ms, 25ms, true});

    bool persistent = controller.detect_persistent_congestion(
        std::vector<flowq::quic::congestion_packet>{
            {0, at(0ms), true, 1200},
            {1, at(100ms), true, 1200}
        },
        at(500ms));

    CHECK_FALSE(persistent);
}

TEST_CASE("congestion controller resets window on persistent congestion") {
    flowq::quic::congestion_controller controller{};
    controller.update_rtt(flowq::quic::rtt_sample{100ms, 0ms, 25ms, true});

    controller.on_persistent_congestion();

    CHECK(controller.congestion_window() == flowq::quic::default_minimum_window());
    CHECK(controller.state() == flowq::quic::congestion_phase::slow_start);
}

TEST_CASE("congestion controller reports can_send based on bytes-in-flight vs window") {
    flowq::quic::congestion_controller controller{};
    const auto window = controller.congestion_window();

    // Fill up to the window
    controller.on_packet_sent(window, true);

    CHECK_FALSE(controller.can_send());
}

TEST_CASE("congestion controller allows sending when bytes-in-flight below window") {
    flowq::quic::congestion_controller controller{};

    controller.on_packet_sent(1000, true);

    CHECK(controller.can_send());
}

TEST_CASE("congestion controller is path-level not per-space") {
    flowq::quic::congestion_controller controller{};

    // Packets from different spaces share the same bytes-in-flight
    controller.on_packet_sent(1200, true);  // Initial
    controller.on_packet_sent(800, true);   // Handshake

    CHECK(controller.bytes_in_flight() == 2000);
}
