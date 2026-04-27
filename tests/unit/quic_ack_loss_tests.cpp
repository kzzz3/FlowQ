#include <flowq/quic/ack_loss.hpp>

#include <catch2/catch_test_macros.hpp>

#include <vector>

TEST_CASE("received packet tracker builds contiguous ACK range") {
    flowq::quic::received_packet_tracker tracker{};

    CHECK(tracker.observe(1));
    CHECK(tracker.observe(2));
    CHECK(tracker.observe(3));
    CHECK(tracker.observe(4));

    auto ack = tracker.to_ack_frame(7);

    CHECK(ack.largest_acknowledged == 4);
    CHECK(ack.ack_delay == 7);
    CHECK(ack.first_ack_range == 3);
    CHECK(ack.ranges.empty());
}

TEST_CASE("received packet tracker normalizes out of order packets") {
    flowq::quic::received_packet_tracker tracker{};

    CHECK(tracker.observe(1));
    CHECK(tracker.observe(4));
    CHECK(tracker.observe(2));
    CHECK(tracker.observe(3));

    auto ack = tracker.to_ack_frame();

    CHECK(ack.largest_acknowledged == 4);
    CHECK(ack.first_ack_range == 3);
    CHECK(ack.ranges.empty());
}

TEST_CASE("received packet tracker builds gapped ACK ranges") {
    flowq::quic::received_packet_tracker tracker{};

    CHECK(tracker.observe(1));
    CHECK(tracker.observe(2));
    CHECK(tracker.observe(5));
    CHECK(tracker.observe(6));

    auto ack = tracker.to_ack_frame();

    CHECK(ack.largest_acknowledged == 6);
    CHECK(ack.first_ack_range == 1);
    REQUIRE(ack.ranges.size() == 1);
    CHECK(ack.ranges[0].gap == 1);
    CHECK(ack.ranges[0].length == 1);
}

TEST_CASE("received packet tracker ignores duplicate observations") {
    flowq::quic::received_packet_tracker tracker{};

    CHECK(tracker.observe(1));
    CHECK(tracker.observe(2));
    CHECK_FALSE(tracker.observe(2));
    CHECK(tracker.observe(3));

    auto ack = tracker.to_ack_frame();

    CHECK(ack.largest_acknowledged == 3);
    CHECK(ack.first_ack_range == 2);
    CHECK(ack.ranges.empty());
}

TEST_CASE("sent packet tracker reports newly acknowledged packets once") {
    flowq::quic::sent_packet_tracker tracker{flowq::quic::packet_number_space::application};
    tracker.on_packet_sent(1, true);
    tracker.on_packet_sent(2, true);
    tracker.on_packet_sent(3, true);

    auto result = tracker.on_ack_received(flowq::quic::ack_frame{3, 0, 1, {}});

    CHECK(result.newly_acknowledged == std::vector<std::uint64_t>{2, 3});
    CHECK(result.newly_lost.empty());

    auto repeated = tracker.on_ack_received(flowq::quic::ack_frame{3, 0, 1, {}});
    CHECK(repeated.newly_acknowledged.empty());
    CHECK(repeated.newly_lost.empty());
}

TEST_CASE("sent packet tracker marks packet threshold losses") {
    flowq::quic::sent_packet_tracker tracker{flowq::quic::packet_number_space::application};
    for (std::uint64_t packet = 1; packet <= 5; ++packet) {
        tracker.on_packet_sent(packet, true);
    }

    auto result = tracker.on_ack_received(flowq::quic::ack_frame{5, 0, 0, {}});

    CHECK(result.newly_acknowledged == std::vector<std::uint64_t>{5});
    CHECK(result.newly_lost == std::vector<std::uint64_t>{1, 2});

    const auto& packets = tracker.packets();
    CHECK(packets[0].state == flowq::quic::sent_packet_state::lost);
    CHECK(packets[1].state == flowq::quic::sent_packet_state::lost);
    CHECK(packets[2].state == flowq::quic::sent_packet_state::outstanding);
    CHECK(packets[3].state == flowq::quic::sent_packet_state::outstanding);
    CHECK(packets[4].state == flowq::quic::sent_packet_state::acknowledged);
}

TEST_CASE("sent packet tracker does not report non ack eliciting packets as lost") {
    flowq::quic::sent_packet_tracker tracker{flowq::quic::packet_number_space::application};
    tracker.on_packet_sent(1, false);
    tracker.on_packet_sent(2, false);
    tracker.on_packet_sent(5, true);

    auto result = tracker.on_ack_received(flowq::quic::ack_frame{5, 0, 0, {}});

    CHECK(result.newly_acknowledged == std::vector<std::uint64_t>{5});
    CHECK(result.newly_lost.empty());
    CHECK(tracker.packets()[0].state == flowq::quic::sent_packet_state::outstanding);
}
