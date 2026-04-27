#include <flowq/quic/ack_loss.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <vector>

using namespace std::chrono_literals;

namespace {

using clock_type = std::chrono::steady_clock;

clock_type::time_point at(std::chrono::milliseconds offset) {
    return clock_type::time_point{offset};
}

} // namespace

TEST_CASE("RTT estimator initializes from first sample") {
    flowq::quic::rtt_estimator estimator{};

    estimator.update(flowq::quic::rtt_sample{100ms, 25ms, 25ms, true});

    CHECK(estimator.has_sample());
    CHECK(estimator.latest_rtt() == 100ms);
    CHECK(estimator.min_rtt() == 100ms);
    CHECK(estimator.smoothed_rtt() == 100ms);
    CHECK(estimator.rtt_variance() == 50ms);
}

TEST_CASE("RTT estimator updates later samples with clamped ACK delay") {
    flowq::quic::rtt_estimator estimator{};
    estimator.update(flowq::quic::rtt_sample{100ms, 0ms, 25ms, true});

    estimator.update(flowq::quic::rtt_sample{130ms, 50ms, 25ms, true});

    CHECK(estimator.latest_rtt() == 130ms);
    CHECK(estimator.min_rtt() == 100ms);
    CHECK(estimator.smoothed_rtt() == 100ms + 625us);
    CHECK(estimator.rtt_variance() == 38ms + 750us);
}

TEST_CASE("RTT estimator ignores ACK delay before handshake confirmation") {
    flowq::quic::rtt_estimator estimator{};
    estimator.update(flowq::quic::rtt_sample{100ms, 0ms, 25ms, true});

    estimator.update(flowq::quic::rtt_sample{130ms, 25ms, 25ms, false});

    CHECK(estimator.smoothed_rtt() == 103ms + 750us);
    CHECK(estimator.rtt_variance() == 45ms);
}

TEST_CASE("time threshold loss marks old ack eliciting packets") {
    flowq::quic::rtt_estimator estimator{};
    estimator.update(flowq::quic::rtt_sample{80ms, 0ms, 25ms, true});

    std::vector<flowq::quic::recovery_packet> packets{
        {flowq::quic::packet_number_space::application, 1, at(0ms), true, flowq::quic::sent_packet_state::outstanding},
        {flowq::quic::packet_number_space::application, 2, at(50ms), true, flowq::quic::sent_packet_state::outstanding},
        {flowq::quic::packet_number_space::application, 3, at(70ms), false, flowq::quic::sent_packet_state::outstanding},
        {flowq::quic::packet_number_space::application, 4, at(80ms), true, flowq::quic::sent_packet_state::acknowledged}
    };

    auto result = flowq::quic::detect_time_threshold_losses(packets, estimator, flowq::quic::packet_number_space::application, 4, at(100ms));

    CHECK(result.newly_lost == std::vector<std::uint64_t>{1});
    REQUIRE(result.earliest_loss_time.has_value());
    CHECK(*result.earliest_loss_time == at(140ms));
    CHECK(packets[0].state == flowq::quic::sent_packet_state::lost);
    CHECK(packets[1].state == flowq::quic::sent_packet_state::outstanding);
    CHECK(packets[2].state == flowq::quic::sent_packet_state::outstanding);
}

TEST_CASE("time threshold loss stays within packet number space") {
    flowq::quic::rtt_estimator estimator{};
    estimator.update(flowq::quic::rtt_sample{80ms, 0ms, 25ms, true});

    std::vector<flowq::quic::recovery_packet> packets{
        {flowq::quic::packet_number_space::initial, 1, at(0ms), true, flowq::quic::sent_packet_state::outstanding},
        {flowq::quic::packet_number_space::application, 1, at(0ms), true, flowq::quic::sent_packet_state::outstanding}
    };

    auto result = flowq::quic::detect_time_threshold_losses(packets, estimator, flowq::quic::packet_number_space::application, 4, at(100ms));

    CHECK(result.newly_lost == std::vector<std::uint64_t>{1});
    CHECK(packets[0].state == flowq::quic::sent_packet_state::outstanding);
    CHECK(packets[1].state == flowq::quic::sent_packet_state::lost);
}

TEST_CASE("PTO deadline uses RTT formula and backoff") {
    flowq::quic::rtt_estimator estimator{};
    estimator.update(flowq::quic::rtt_sample{100ms, 0ms, 25ms, true});

    flowq::quic::pto_config config{25ms, 333ms, 1, true};
    auto deadline = flowq::quic::pto_deadline(at(1000ms), estimator, flowq::quic::packet_number_space::application, config);

    CHECK(deadline == at(1650ms));
}

TEST_CASE("PTO deadline uses initial RTT without samples") {
    flowq::quic::rtt_estimator estimator{};

    flowq::quic::pto_config config{25ms, 333ms, 0, true};
    auto deadline = flowq::quic::pto_deadline(at(1000ms), estimator, flowq::quic::packet_number_space::application, config);

    CHECK(deadline == at(2024ms));
}

TEST_CASE("loss timer scheduler prefers loss time before PTO") {
    flowq::quic::rtt_estimator estimator{};
    estimator.update(flowq::quic::rtt_sample{80ms, 0ms, 25ms, true});

    std::vector<flowq::quic::recovery_packet> packets{
        {flowq::quic::packet_number_space::application, 1, at(50ms), true, flowq::quic::sent_packet_state::outstanding}
    };

    flowq::quic::pto_config config{25ms, 333ms, 0, true};
    auto timer = flowq::quic::next_loss_timer(packets, estimator, at(100ms), flowq::quic::packet_number_space::application, config);

    CHECK(timer.mode == flowq::quic::loss_timer_mode::loss_time);
    REQUIRE(timer.deadline.has_value());
    CHECK(*timer.deadline == at(140ms));
}

TEST_CASE("loss timer scheduler returns no timer without outstanding ack eliciting packets") {
    flowq::quic::rtt_estimator estimator{};
    estimator.update(flowq::quic::rtt_sample{80ms, 0ms, 25ms, true});

    std::vector<flowq::quic::recovery_packet> packets{
        {flowq::quic::packet_number_space::application, 1, at(50ms), false, flowq::quic::sent_packet_state::outstanding}
    };

    flowq::quic::pto_config config{25ms, 333ms, 0, true};
    auto timer = flowq::quic::next_loss_timer(packets, estimator, at(100ms), flowq::quic::packet_number_space::application, config);

    CHECK(timer.mode == flowq::quic::loss_timer_mode::none);
    CHECK_FALSE(timer.deadline.has_value());
}

TEST_CASE("loss timer scheduler anchors PTO to last ack eliciting send time") {
    flowq::quic::rtt_estimator estimator{};
    std::vector<flowq::quic::recovery_packet> packets{
        {flowq::quic::packet_number_space::handshake, 1, at(0ms), true, flowq::quic::sent_packet_state::outstanding}
    };

    flowq::quic::pto_config config{0ms, 333ms, 0, false};
    auto first = flowq::quic::next_loss_timer(packets, estimator, at(100ms), flowq::quic::packet_number_space::handshake, config);
    auto later = flowq::quic::next_loss_timer(packets, estimator, at(200ms), flowq::quic::packet_number_space::handshake, config);

    CHECK(first.mode == flowq::quic::loss_timer_mode::pto);
    REQUIRE(first.deadline.has_value());
    CHECK(*first.deadline == at(999ms));
    CHECK(later.deadline == first.deadline);
}

TEST_CASE("loss timer scheduler does not arm application PTO before handshake confirmation") {
    flowq::quic::rtt_estimator estimator{};
    std::vector<flowq::quic::recovery_packet> packets{
        {flowq::quic::packet_number_space::application, 1, at(50ms), true, flowq::quic::sent_packet_state::outstanding}
    };

    flowq::quic::pto_config config{25ms, 333ms, 0, false};
    auto timer = flowq::quic::next_loss_timer(packets, estimator, at(100ms), flowq::quic::packet_number_space::application, config);

    CHECK(timer.mode == flowq::quic::loss_timer_mode::none);
    CHECK_FALSE(timer.deadline.has_value());
}
