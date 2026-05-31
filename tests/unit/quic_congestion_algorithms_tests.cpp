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

TEST_CASE("congestion_algorithm public API only advertises NewReno") {
    CHECK(static_cast<int>(flowq::quic::congestion_algorithm::new_reno) == 0);
}
