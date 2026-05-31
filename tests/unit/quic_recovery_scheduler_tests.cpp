#include "plaintext_packet_protector.hpp"
#include <flowq/context.hpp>
#include <flowq/quic/recovery_scheduler.hpp>
#include <flowq/quic/session.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using clock_type = std::chrono::steady_clock;

clock_type::time_point at(std::chrono::milliseconds offset) {
    return clock_type::time_point{offset};
}

std::vector<std::byte> bytes(std::initializer_list<unsigned int> values) {
    std::vector<std::byte> output;
    output.reserve(values.size());
    for (auto value : values) {
        output.push_back(static_cast<std::byte>(value & 0xffU));
    }
    return output;
}

flowq::quic::connection_id cid(std::initializer_list<unsigned int> values) {
    return flowq::quic::connection_id{flowq::buffer{bytes(values)}};
}

flowq::quic::session_config make_config(
    flowq::quic::connection_id local,
    flowq::quic::connection_id remote,
    flowq::endpoint peer,
    const flowq::quic::test::plaintext_packet_protector_set& protector) {
    flowq::quic::session_config config{};
    config.role = flowq::quic::connection_role::client;
    config.local_connection_id = std::move(local);
    config.remote_connection_id = std::move(remote);
    config.peer = std::move(peer);
    config.initial_tx_protector = &protector.initial;
    config.initial_rx_protector = &protector.initial;
    config.handshake_tx_protector = &protector.handshake;
    config.handshake_rx_protector = &protector.handshake;
    config.application_tx_protector = &protector.application;
    config.application_rx_protector = &protector.application;
    return config;
}

struct fake_recovery_session {
    std::optional<flowq::quic::connection_recovery_timer> timer;
    std::optional<flowq::quic::packet_number_space> expired_space;
    std::optional<clock_type::time_point> expired_at;

    [[nodiscard]] std::optional<flowq::quic::connection_recovery_timer> next_recovery_timer() const {
        return timer;
    }

    [[nodiscard]] flowq::quic::connection_recovery_result on_recovery_timer(
        flowq::quic::packet_number_space space,
        clock_type::time_point now) {
        expired_space = space;
        expired_at = now;
        return flowq::quic::connection_recovery_result{space, {7}, at(20ms)};
    }
};

struct scheduler_receiver {
    std::optional<flowq::quic::recovery_scheduler_result>* result;
    bool* stopped{};

    friend void set_value(scheduler_receiver&& receiver, flowq::quic::recovery_scheduler_result result) noexcept {
        receiver.result->emplace(std::move(result));
    }

    friend void set_error(scheduler_receiver&&, flowq::error) noexcept {}

    friend void set_stopped(scheduler_receiver&& receiver) noexcept {
        if (receiver.stopped != nullptr) {
            *receiver.stopped = true;
        }
    }
};

} // namespace

TEST_CASE("recovery scheduler completes immediately when session has no recovery timer") {
    flowq::context context{};
    flowq::quic::test::plaintext_packet_protector_set protector{};
    flowq::quic::session session{make_config(
        cid({0x01}),
        cid({0x02}),
        flowq::endpoint{"server", 4433, "hq-interop"},
        protector)};
    std::optional<flowq::quic::recovery_scheduler_result> result;

    auto sender = flowq::quic::schedule_recovery(context, session, at(0ms));
    auto operation = sender.connect(scheduler_receiver{&result});

    operation.start();
    context.io_context().run();

    REQUIRE(result.has_value());
    REQUIRE(result->ok());
    CHECK_FALSE(result->fired.has_value());
    CHECK_FALSE(result->recovery_result.has_value());
}

TEST_CASE("recovery scheduler invokes on_recovery_timer when ASIO timer fires") {
    flowq::context context{};
    fake_recovery_session session{flowq::quic::connection_recovery_timer{
        flowq::quic::packet_number_space::initial,
        flowq::quic::loss_timer_mode::pto,
        at(5ms)
    }};
    std::optional<flowq::quic::recovery_scheduler_result> result;

    auto sender = flowq::quic::schedule_recovery(context, session, at(0ms));
    auto operation = sender.connect(scheduler_receiver{&result});

    operation.start();
    context.io_context().run();

    REQUIRE(result.has_value());
    REQUIRE(result->ok());
    REQUIRE(result->fired.has_value());
    REQUIRE(result->recovery_result.has_value());
    CHECK(result->fired->space == flowq::quic::packet_number_space::initial);
    CHECK(result->recovery_result->newly_lost == std::vector<std::uint64_t>{7});
    REQUIRE(session.expired_space.has_value());
    CHECK(*session.expired_space == flowq::quic::packet_number_space::initial);
    REQUIRE(session.expired_at.has_value());
    CHECK(*session.expired_at == at(5ms));
}

TEST_CASE("recovery scheduler cancellation maps to stopped") {
    flowq::context context{};
    fake_recovery_session session{flowq::quic::connection_recovery_timer{
        flowq::quic::packet_number_space::initial,
        flowq::quic::loss_timer_mode::pto,
        clock_type::now() + 1h
    }};
    std::optional<flowq::quic::recovery_scheduler_result> result;
    bool stopped = false;

    auto sender = flowq::quic::schedule_recovery(context, session, clock_type::now());
    auto operation = sender.connect(scheduler_receiver{&result, &stopped});

    operation.start();
    operation.cancel();
    context.io_context().run();

    CHECK(stopped);
    CHECK_FALSE(result.has_value());
}
