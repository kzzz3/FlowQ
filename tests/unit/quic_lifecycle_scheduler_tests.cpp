#include "plaintext_packet_protector.hpp"
#include <flowq/context.hpp>
#include <flowq/quic/lifecycle_scheduler.hpp>
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

flowq::buffer text(std::string value) {
    std::vector<std::byte> output;
    output.reserve(value.size());
    for (auto character : value) {
        output.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return flowq::buffer{output};
}

flowq::quic::session_config make_config(
    flowq::quic::connection_id local,
    flowq::quic::connection_id remote,
    flowq::endpoint peer,
    const flowq::quic::packet_protector& protector) {
    flowq::quic::session_config config{};
    config.role = flowq::quic::connection_role::client;
    config.local_connection_id = std::move(local);
    config.remote_connection_id = std::move(remote);
    config.peer = std::move(peer);
    config.initial_tx_protector = &protector;
    config.initial_rx_protector = &protector;
    config.handshake_tx_protector = &protector;
    config.handshake_rx_protector = &protector;
    config.application_tx_protector = &protector;
    config.application_rx_protector = &protector;
    config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    return config;
}

struct fake_lifecycle_session {
    std::optional<flowq::quic::connection_lifecycle_timer> timer;
    std::optional<flowq::quic::connection_lifecycle_timer_kind> expired_kind;
    std::optional<clock_type::time_point> expired_at;
    flowq::quic::session_send_result expiry_result{};

    [[nodiscard]] std::optional<flowq::quic::connection_lifecycle_timer> next_lifecycle_timer(clock_type::time_point) {
        return timer;
    }

    [[nodiscard]] flowq::quic::session_send_result on_lifecycle_timer(
        flowq::quic::connection_lifecycle_timer_kind kind,
        clock_type::time_point now) {
        expired_kind = kind;
        expired_at = now;
        return expiry_result;
    }
};

struct scheduler_receiver {
    std::optional<flowq::quic::lifecycle_scheduler_result>* result;
    bool* stopped{};

    friend void set_value(scheduler_receiver&& receiver, flowq::quic::lifecycle_scheduler_result result) noexcept {
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

TEST_CASE("lifecycle scheduler completes immediately when session has no lifecycle timer") {
    flowq::context context{};
    fake_lifecycle_session session{};
    std::optional<flowq::quic::lifecycle_scheduler_result> result;

    auto sender = flowq::quic::schedule_lifecycle(context, session, at(0ms));
    auto operation = sender.connect(scheduler_receiver{&result});

    operation.start();
    context.io_context().run();

    REQUIRE(result.has_value());
    REQUIRE(result->ok());
    CHECK_FALSE(result->fired.has_value());
    CHECK_FALSE(result->send_result.has_value());
}

TEST_CASE("lifecycle scheduler invokes on_lifecycle_timer when ASIO timer fires") {
    flowq::context context{};
    fake_lifecycle_session session{flowq::quic::connection_lifecycle_timer{
        flowq::quic::connection_lifecycle_timer_kind::idle,
        at(5ms)
    }};
    session.expiry_result.error = flowq::error{flowq::error_code::timeout, "idle timeout expired"};
    session.expiry_result.closes.push_back(flowq::quic::close_action{session.expiry_result.error});
    std::optional<flowq::quic::lifecycle_scheduler_result> result;

    auto sender = flowq::quic::schedule_lifecycle(context, session, at(0ms));
    auto operation = sender.connect(scheduler_receiver{&result});

    operation.start();
    context.io_context().run();

    REQUIRE(result.has_value());
    REQUIRE(result->ok());
    REQUIRE(result->fired.has_value());
    REQUIRE(result->send_result.has_value());
    CHECK(result->fired->kind == flowq::quic::connection_lifecycle_timer_kind::idle);
    CHECK_FALSE(result->send_result->ok());
    CHECK(result->send_result->error.code() == flowq::error_code::timeout);
    REQUIRE(session.expired_kind.has_value());
    CHECK(*session.expired_kind == flowq::quic::connection_lifecycle_timer_kind::idle);
    REQUIRE(session.expired_at.has_value());
    CHECK(*session.expired_at == at(5ms));
}

TEST_CASE("lifecycle scheduler cancellation maps to stopped") {
    flowq::context context{};
    fake_lifecycle_session session{flowq::quic::connection_lifecycle_timer{
        flowq::quic::connection_lifecycle_timer_kind::idle,
        clock_type::now() + 1h
    }};
    std::optional<flowq::quic::lifecycle_scheduler_result> result;
    bool stopped = false;

    auto sender = flowq::quic::schedule_lifecycle(context, session, clock_type::now());
    auto operation = sender.connect(scheduler_receiver{&result, &stopped});

    operation.start();
    operation.cancel();
    context.io_context().run();

    CHECK(stopped);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("lifecycle scheduler drives real session idle timeout close") {
    flowq::context context{};
    flowq::quic::test::plaintext_packet_protector protector{};
    auto config = make_config(
        cid({0x01}),
        cid({0x02}),
        flowq::endpoint{"server", 4433, "hq-interop"},
        protector);
    config.max_idle_timeout = 5ms;
    flowq::quic::session session{std::move(config)};
    std::optional<flowq::quic::lifecycle_scheduler_result> result;

    REQUIRE(session.append_stream_data(0, text("hello")).ok());
    REQUIRE(session.queue_stream_data({0}).ok());
    REQUIRE(session.flush(at(0ms)).ok());

    auto sender = flowq::quic::schedule_lifecycle(context, session, at(0ms));
    auto operation = sender.connect(scheduler_receiver{&result});

    operation.start();
    context.io_context().run();

    REQUIRE(result.has_value());
    REQUIRE(result->ok());
    REQUIRE(result->fired.has_value());
    REQUIRE(result->send_result.has_value());
    CHECK(result->fired->kind == flowq::quic::connection_lifecycle_timer_kind::idle);
    CHECK(result->fired->deadline == at(5ms));
    CHECK_FALSE(result->send_result->ok());
    REQUIRE(result->send_result->closes.size() == 1);
    CHECK(result->send_result->error.code() == flowq::error_code::timeout);
}
