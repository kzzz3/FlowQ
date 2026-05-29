#include <flowq/quic/connection_packet_spaces.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("connection packet spaces keep per-space state isolated") {
    flowq::quic::connection_packet_spaces spaces;

    auto& initial = spaces.get(flowq::quic::packet_number_space::initial);
    auto& handshake = spaces.get(flowq::quic::packet_number_space::handshake);
    auto& application = spaces.get(flowq::quic::packet_number_space::application);

    initial.next_packet_number = 7;
    handshake.next_packet_number = 11;
    application.next_packet_number = 13;
    REQUIRE(initial.received.observe(1));
    REQUIRE(handshake.received.observe(2));
    REQUIRE(application.received.observe(3));
    initial.sent.on_packet_sent(4, true);
    handshake.sent.on_packet_sent(5, false);
    application.sent.on_packet_sent(6, true);
    initial.largest_acknowledged = 4;
    handshake.largest_acknowledged = 5;
    application.largest_acknowledged = 6;

    CHECK(spaces.get(flowq::quic::packet_number_space::initial).next_packet_number == 7);
    CHECK(spaces.get(flowq::quic::packet_number_space::handshake).next_packet_number == 11);
    CHECK(spaces.get(flowq::quic::packet_number_space::application).next_packet_number == 13);
    CHECK(spaces.get(flowq::quic::packet_number_space::initial).received.to_ack_frame().largest_acknowledged == 1);
    CHECK(spaces.get(flowq::quic::packet_number_space::handshake).received.to_ack_frame().largest_acknowledged == 2);
    CHECK(spaces.get(flowq::quic::packet_number_space::application).received.to_ack_frame().largest_acknowledged == 3);
    CHECK(spaces.get(flowq::quic::packet_number_space::initial).sent.packets()[0].space == flowq::quic::packet_number_space::initial);
    CHECK(spaces.get(flowq::quic::packet_number_space::handshake).sent.packets()[0].space == flowq::quic::packet_number_space::handshake);
    CHECK(spaces.get(flowq::quic::packet_number_space::application).sent.packets()[0].space == flowq::quic::packet_number_space::application);
    CHECK(spaces.get(flowq::quic::packet_number_space::initial).largest_acknowledged == 4);
    CHECK(spaces.get(flowq::quic::packet_number_space::handshake).largest_acknowledged == 5);
    CHECK(spaces.get(flowq::quic::packet_number_space::application).largest_acknowledged == 6);
}

TEST_CASE("connection packet spaces clear only the selected packet space") {
    flowq::quic::connection_packet_spaces spaces;

    REQUIRE(spaces.get(flowq::quic::packet_number_space::initial).received.observe(1));
    spaces.get(flowq::quic::packet_number_space::initial).sent.on_packet_sent(2, true);
    spaces.get(flowq::quic::packet_number_space::initial).largest_acknowledged = 2;
    REQUIRE(spaces.get(flowq::quic::packet_number_space::handshake).received.observe(3));
    REQUIRE(spaces.get(flowq::quic::packet_number_space::application).received.observe(4));

    spaces.clear(flowq::quic::packet_number_space::initial);

    CHECK(spaces.get(flowq::quic::packet_number_space::initial).received.empty());
    CHECK(spaces.get(flowq::quic::packet_number_space::initial).sent.packets().empty());
    CHECK_FALSE(spaces.get(flowq::quic::packet_number_space::initial).largest_acknowledged.has_value());
    CHECK_FALSE(spaces.get(flowq::quic::packet_number_space::handshake).received.empty());
    CHECK_FALSE(spaces.get(flowq::quic::packet_number_space::application).received.empty());
}
