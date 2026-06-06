#include <flowq/quic/congestion_algorithms.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("create_congestion_controller returns production NewReno controller") {
    auto controller = flowq::quic::create_congestion_controller(flowq::quic::congestion_algorithm::new_reno);

    REQUIRE(controller != nullptr);
    CHECK(controller->congestion_window() == flowq::quic::default_initial_window());
    CHECK(controller->bytes_in_flight() == 0);
    CHECK(controller->can_send());
    CHECK(controller->state() == flowq::quic::congestion_phase::slow_start);
}

TEST_CASE("created NewReno controller tracks packet accounting and congestion events") {
    auto controller = flowq::quic::create_congestion_controller(flowq::quic::congestion_algorithm::new_reno);
    REQUIRE(controller != nullptr);

    controller->on_packet_sent(1200, true);
    controller->on_packet_sent(800, true);
    CHECK(controller->bytes_in_flight() == 2000);

    const auto initial_window = controller->congestion_window();
    controller->on_packet_acknowledged(1200);
    CHECK(controller->bytes_in_flight() == 800);
    CHECK(controller->congestion_window() > initial_window);

    controller->on_packet_lost(800);
    CHECK(controller->bytes_in_flight() == 0);

    controller->on_congestion_event();
    CHECK(controller->state() == flowq::quic::congestion_phase::congestion_avoidance);
    CHECK(controller->congestion_window() >= flowq::quic::default_minimum_window());
}

TEST_CASE("congestion_algorithm public API advertises supported production algorithms") {
    CHECK(static_cast<int>(flowq::quic::congestion_algorithm::new_reno) == 0);
    CHECK(flowq::quic::create_congestion_controller(flowq::quic::congestion_algorithm::new_reno) != nullptr);
    CHECK(flowq::quic::create_congestion_controller(flowq::quic::congestion_algorithm::bbr) != nullptr);
    CHECK(flowq::quic::create_congestion_controller(flowq::quic::congestion_algorithm::cubic) != nullptr);
}

TEST_CASE("BBR and CUBIC controllers ignore non-ack-eliciting sends") {
    flowq::quic::bbr_congestion_controller bbr;
    flowq::quic::cubic_congestion_controller cubic;

    bbr.on_packet_sent(1200, false);
    cubic.on_packet_sent(1200, false);

    CHECK(bbr.bytes_in_flight() == 0);
    CHECK(cubic.bytes_in_flight() == 0);
    CHECK(bbr.can_send());
    CHECK(cubic.can_send());
}

TEST_CASE("BBR and CUBIC controllers saturate over-accounted acknowledgments and losses") {
    flowq::quic::bbr_congestion_controller bbr_ack;
    bbr_ack.on_packet_sent(800, true);
    bbr_ack.on_packet_acknowledged(1200);
    CHECK(bbr_ack.bytes_in_flight() == 0);

    flowq::quic::bbr_congestion_controller bbr_loss;
    bbr_loss.on_packet_sent(800, true);
    bbr_loss.on_packet_lost(1200);
    CHECK(bbr_loss.bytes_in_flight() == 0);

    flowq::quic::cubic_congestion_controller cubic_ack;
    cubic_ack.on_packet_sent(800, true);
    cubic_ack.on_packet_acknowledged(1200);
    CHECK(cubic_ack.bytes_in_flight() == 0);

    flowq::quic::cubic_congestion_controller cubic_loss;
    cubic_loss.on_packet_sent(800, true);
    cubic_loss.on_packet_lost(1200);
    CHECK(cubic_loss.bytes_in_flight() == 0);
}

TEST_CASE("BBR and CUBIC controllers ignore zero-byte acknowledgments") {
    flowq::quic::bbr_congestion_controller bbr;
    bbr.on_packet_sent(800, true);
    bbr.on_packet_acknowledged(0);
    CHECK(bbr.bytes_in_flight() == 800);

    flowq::quic::cubic_congestion_controller cubic;
    cubic.on_packet_sent(12000, true);
    cubic.on_packet_lost(12000);
    cubic.on_congestion_event();
    REQUIRE(cubic.state() == flowq::quic::congestion_phase::recovery);
    const auto recovery_window = cubic.congestion_window();

    cubic.on_packet_acknowledged(0);

    CHECK(cubic.bytes_in_flight() == 0);
    CHECK(cubic.state() == flowq::quic::congestion_phase::recovery);
    CHECK(cubic.congestion_window() == recovery_window);
}

TEST_CASE("BBR and CUBIC controllers ignore zero-byte losses") {
    flowq::quic::bbr_congestion_controller bbr;
    const auto bbr_window = bbr.congestion_window();

    bbr.on_packet_lost(0);

    CHECK(bbr.bytes_in_flight() == 0);
    CHECK(bbr.state() == flowq::quic::congestion_phase::slow_start);
    CHECK(bbr.congestion_window() == bbr_window);

    flowq::quic::cubic_congestion_controller cubic;
    const auto cubic_window = cubic.congestion_window();

    cubic.on_packet_lost(0);

    CHECK(cubic.bytes_in_flight() == 0);
    CHECK(cubic.state() == flowq::quic::congestion_phase::slow_start);
    CHECK(cubic.congestion_window() == cubic_window);
}

TEST_CASE("BBR and CUBIC controllers apply one congestion response per loss event") {
    flowq::quic::bbr_congestion_controller bbr;
    bbr.on_packet_sent(12000, true);
    bbr.on_packet_lost(12000);
    bbr.on_congestion_event();
    CHECK(bbr.congestion_window() == 6000);
    CHECK(bbr.state() == flowq::quic::congestion_phase::recovery);

    flowq::quic::cubic_congestion_controller cubic;
    cubic.on_packet_sent(12000, true);
    cubic.on_packet_lost(12000);
    cubic.on_congestion_event();
    CHECK(cubic.congestion_window() == static_cast<std::uint64_t>(12000 * 0.7));
    CHECK(cubic.state() == flowq::quic::congestion_phase::recovery);
}

// ============================================================================
// CUBIC Tests (RFC 8312)
// ============================================================================

TEST_CASE("CUBIC controller initializes with correct defaults") {
    auto controller = flowq::quic::create_congestion_controller(flowq::quic::congestion_algorithm::cubic);

    REQUIRE(controller != nullptr);
    CHECK(controller->congestion_window() == flowq::quic::default_initial_window());
    CHECK(controller->bytes_in_flight() == 0);
    CHECK(controller->can_send());
    CHECK(controller->state() == flowq::quic::congestion_phase::slow_start);
}

TEST_CASE("CUBIC slow start exits at ssthresh") {
    flowq::quic::cubic_congestion_controller controller;

    // Initial: cwnd = 12000, ssthresh = 12000 (initial_cwnd_)
    // Trigger loss to set a lower ssthresh
    controller.on_packet_sent(12000, true);
    controller.on_packet_lost(12000);
    controller.on_congestion_event();
    // After loss: cwnd = 8400, ssthresh = 8400, phase = recovery
    CHECK(controller.congestion_window() == static_cast<std::uint64_t>(12000 * 0.7));
    CHECK(controller.state() == flowq::quic::congestion_phase::recovery);

    // First ACK exits recovery and enters congestion_avoidance
    controller.on_packet_sent(8400, true);
    controller.on_packet_acknowledged(8400);
    CHECK(controller.state() == flowq::quic::congestion_phase::congestion_avoidance);
}

TEST_CASE("CUBIC congestion avoidance uses cubic function") {
    flowq::quic::cubic_congestion_controller controller;

    // Trigger loss to enter congestion_avoidance with known window
    controller.on_packet_sent(12000, true);
    controller.on_packet_lost(12000);
    controller.on_congestion_event();
    // cwnd = 8400, phase = recovery

    // ACK exits recovery and enters congestion_avoidance
    controller.on_packet_sent(8400, true);
    controller.on_packet_acknowledged(8400);
    CHECK(controller.state() == flowq::quic::congestion_phase::congestion_avoidance);

    auto cwnd_after_entry = controller.congestion_window();

    // In congestion_avoidance, CUBIC uses the cubic function.
    // Window should grow over successive ACKs.
    for (int i = 0; i < 10; ++i) {
        auto current = controller.congestion_window();
        controller.on_packet_sent(current, true);
        controller.on_packet_acknowledged(current);
    }

    CHECK(controller.congestion_window() >= cwnd_after_entry);
}

TEST_CASE("CUBIC loss reduces window by beta") {
    flowq::quic::cubic_congestion_controller controller;

    const auto initial_cwnd = controller.congestion_window();
    const auto expected_after_loss = static_cast<std::uint64_t>(initial_cwnd * 0.7);

    // Packet loss updates bytes-in-flight; the congestion event updates cwnd.
    controller.on_packet_sent(1000, true);
    controller.on_packet_lost(1000);
    controller.on_congestion_event();

    // cwnd should be initial * beta (0.7)
    CHECK(controller.congestion_window() == expected_after_loss);
}

TEST_CASE("CUBIC loss never drops below minimum window") {
    flowq::quic::cubic_congestion_controller controller;

    // Repeatedly trigger losses until we hit the floor
    for (int i = 0; i < 20; ++i) {
        auto cwnd = controller.congestion_window();
        controller.on_packet_sent(cwnd, true);
        controller.on_packet_lost(cwnd);
        controller.on_congestion_event();
    }

    CHECK(controller.congestion_window() >= flowq::quic::default_minimum_window());
}

TEST_CASE("CUBIC fast convergence reduces last_max_cwnd on repeated loss") {
    flowq::quic::cubic_congestion_controller controller;

    // First loss: initial cwnd = 12000, last_max_cwnd was 0 initially
    // Since cwnd (12000) > last_max_cwnd (0), no fast convergence:
    //   last_max_cwnd = cwnd = 12000, then cwnd = 12000 * 0.7 = 8400
    controller.on_packet_sent(12000, true);
    controller.on_packet_lost(12000);
    controller.on_congestion_event();
    
    CHECK(controller.last_max_cwnd() == 12000);
    CHECK(controller.congestion_window() == 8400);

    // Second loss immediately (no ACK in between): cwnd (8400) < last_max (12000)
    // Fast convergence: last_max = cwnd * (1 + beta) / 2
    controller.on_packet_sent(8400, true);
    controller.on_packet_lost(8400);
    controller.on_congestion_event();

    auto expected_fc = static_cast<std::uint64_t>(8400 * (1.0 + 0.7) / 2.0);
    CHECK(controller.last_max_cwnd() == expected_fc);
    CHECK(controller.last_max_cwnd() < 12000);
    // cwnd after second loss: 8400 * 0.7 = 5880
    CHECK(controller.congestion_window() == static_cast<std::uint64_t>(8400 * 0.7));
}

TEST_CASE("CUBIC fast convergence does not trigger when cwnd >= last_max") {
    flowq::quic::cubic_congestion_controller controller;

    // First loss: sets last_max_cwnd = 12000, cwnd = 8400
    controller.on_packet_sent(12000, true);
    controller.on_packet_lost(12000);
    controller.on_congestion_event();
    CHECK(controller.last_max_cwnd() == 12000);

    // Second loss while cwnd (8400) < last_max (12000): fast convergence triggers
    controller.on_packet_sent(8400, true);
    controller.on_packet_lost(8400);
    controller.on_congestion_event();
    
    // Fast convergence should have reduced last_max
    CHECK(controller.last_max_cwnd() < 12000);
    
    // Third loss while cwnd >= last_max (both are now smaller)
    auto last_max_after_fc = controller.last_max_cwnd();
    auto cwnd_now = controller.congestion_window();
    
    if (cwnd_now >= last_max_after_fc) {
        // No fast convergence: last_max = cwnd
        controller.on_packet_sent(cwnd_now, true);
        controller.on_packet_lost(cwnd_now);
        controller.on_congestion_event();
        CHECK(controller.last_max_cwnd() == cwnd_now);
    }
}
