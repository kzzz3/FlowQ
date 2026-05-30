#include <flowq/quic/congestion_algorithms.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("bbr_congestion_controller starts with initial window") {
    flowq::quic::bbr_congestion_controller controller;

    CHECK(controller.congestion_window() == flowq::quic::default_initial_window());
    CHECK(controller.bytes_in_flight() == 0);
    CHECK(controller.can_send());
}

TEST_CASE("bbr_congestion_controller tracks bytes in flight") {
    flowq::quic::bbr_congestion_controller controller;

    controller.on_packet_sent(1200, true);
    CHECK(controller.bytes_in_flight() == 1200);

    controller.on_packet_sent(800, true);
    CHECK(controller.bytes_in_flight() == 2000);
}

TEST_CASE("bbr_congestion_controller ignores non-ack-eliciting packets") {
    flowq::quic::bbr_congestion_controller controller;

    controller.on_packet_sent(1200, false);
    CHECK(controller.bytes_in_flight() == 0);
}

TEST_CASE("bbr_congestion_controller reduces bytes in flight on ACK") {
    flowq::quic::bbr_congestion_controller controller;

    controller.on_packet_sent(1200, true);
    controller.on_packet_sent(800, true);
    controller.on_packet_acknowledged(1200);

    CHECK(controller.bytes_in_flight() == 800);
}

TEST_CASE("bbr_congestion_controller reduces bytes in flight on loss") {
    flowq::quic::bbr_congestion_controller controller;

    controller.on_packet_sent(1200, true);
    controller.on_packet_sent(800, true);
    controller.on_packet_lost(1200);

    CHECK(controller.bytes_in_flight() == 800);
}

TEST_CASE("bbr_congestion_controller reduces window on congestion event") {
    flowq::quic::bbr_congestion_controller controller;
    auto initial_window = controller.congestion_window();

    controller.on_congestion_event();

    CHECK(controller.congestion_window() < initial_window);
    CHECK(controller.congestion_window() >= flowq::quic::default_minimum_window());
}

TEST_CASE("bbr_congestion_controller can_send reflects window") {
    flowq::quic::bbr_congestion_controller controller;

    CHECK(controller.can_send());

    // Fill up to window
    controller.on_packet_sent(controller.congestion_window(), true);
    CHECK_FALSE(controller.can_send());
}

TEST_CASE("cubic_congestion_controller starts with initial window") {
    flowq::quic::cubic_congestion_controller controller;

    CHECK(controller.congestion_window() == flowq::quic::default_initial_window());
    CHECK(controller.bytes_in_flight() == 0);
    CHECK(controller.can_send());
    CHECK(controller.state() == flowq::quic::congestion_phase::slow_start);
}

TEST_CASE("cubic_congestion_controller tracks bytes in flight") {
    flowq::quic::cubic_congestion_controller controller;

    controller.on_packet_sent(1200, true);
    CHECK(controller.bytes_in_flight() == 1200);
}

TEST_CASE("cubic_congestion_controller grows window in slow start") {
    flowq::quic::cubic_congestion_controller controller;
    auto initial_window = controller.congestion_window();

    controller.on_packet_sent(1200, true);
    controller.on_packet_acknowledged(1200);

    CHECK(controller.congestion_window() > initial_window);
}

TEST_CASE("cubic_congestion_controller transitions to congestion avoidance") {
    flowq::quic::cubic_congestion_controller controller;

    controller.on_congestion_event();

    // Now in congestion avoidance
    controller.on_packet_sent(1200, true);
    controller.on_packet_acknowledged(1200);

    CHECK(controller.state() == flowq::quic::congestion_phase::congestion_avoidance);
}

TEST_CASE("cubic_congestion_controller reduces window on congestion event") {
    flowq::quic::cubic_congestion_controller controller;
    auto initial_window = controller.congestion_window();

    controller.on_congestion_event();

    // CUBIC reduces by beta factor (0.7)
    CHECK(controller.congestion_window() < initial_window);
    CHECK(controller.congestion_window() >= flowq::quic::default_minimum_window());
}

TEST_CASE("cubic_congestion_controller can_send reflects window") {
    flowq::quic::cubic_congestion_controller controller;

    CHECK(controller.can_send());

    controller.on_packet_sent(controller.congestion_window(), true);
    CHECK_FALSE(controller.can_send());
}

TEST_CASE("cubic_congestion_controller reduces bytes in flight on ACK") {
    flowq::quic::cubic_congestion_controller controller;

    controller.on_packet_sent(1200, true);
    controller.on_packet_acknowledged(1200);

    CHECK(controller.bytes_in_flight() == 0);
}

TEST_CASE("cubic_congestion_controller reduces bytes in flight on loss") {
    flowq::quic::cubic_congestion_controller controller;

    controller.on_packet_sent(1200, true);
    controller.on_packet_lost(1200);

    CHECK(controller.bytes_in_flight() == 0);
}

TEST_CASE("create_congestion_controller returns nullptr for new_reno") {
    auto controller = flowq::quic::create_congestion_controller(flowq::quic::congestion_algorithm::new_reno);
    CHECK(controller == nullptr);
}

TEST_CASE("create_congestion_controller returns bbr controller") {
    auto controller = flowq::quic::create_congestion_controller(flowq::quic::congestion_algorithm::bbr);
    REQUIRE(controller != nullptr);
    CHECK(controller->congestion_window() == flowq::quic::default_initial_window());
}

TEST_CASE("create_congestion_controller returns cubic controller") {
    auto controller = flowq::quic::create_congestion_controller(flowq::quic::congestion_algorithm::cubic);
    REQUIRE(controller != nullptr);
    CHECK(controller->congestion_window() == flowq::quic::default_initial_window());
}

TEST_CASE("congestion_algorithm enum values are correct") {
    CHECK(static_cast<int>(flowq::quic::congestion_algorithm::new_reno) == 0);
    CHECK(static_cast<int>(flowq::quic::congestion_algorithm::bbr) == 1);
    CHECK(static_cast<int>(flowq::quic::congestion_algorithm::cubic) == 2);
}
