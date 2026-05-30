#include <flowq/context.hpp>
#include <flowq/quic/session.hpp>
#include <flowq/quic/timer_scheduler.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <string_view>
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
    config.initial_protector = &protector;
    config.handshake_protector = &protector;
    config.application_protector = &protector;
    config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    return config;
}

struct fake_quic_timer_session {
    std::optional<flowq::quic::connection_recovery_timer> recovery_timer;
    std::optional<flowq::quic::connection_lifecycle_timer> lifecycle_timer;
    std::optional<flowq::quic::packet_number_space> expired_recovery_space;
    std::optional<flowq::quic::connection_lifecycle_timer_kind> expired_lifecycle_kind;
    std::optional<clock_type::time_point> expired_at;
    flowq::quic::connection_recovery_result recovery_expiry_result{
        flowq::quic::packet_number_space::initial,
        {7},
        at(20ms)
    };
    flowq::quic::session_send_result lifecycle_expiry_result{};

    [[nodiscard]] std::optional<flowq::quic::connection_recovery_timer> next_recovery_timer(clock_type::time_point) {
        return recovery_timer;
    }

    [[nodiscard]] std::optional<flowq::quic::connection_lifecycle_timer> next_lifecycle_timer(clock_type::time_point) {
        return lifecycle_timer;
    }

    [[nodiscard]] flowq::quic::connection_recovery_result on_recovery_timer(
        flowq::quic::packet_number_space space,
        clock_type::time_point now) {
        expired_recovery_space = space;
        expired_at = now;
        auto result = recovery_expiry_result;
        result.space = space;
        return result;
    }

    [[nodiscard]] flowq::quic::session_send_result on_lifecycle_timer(
        flowq::quic::connection_lifecycle_timer_kind kind,
        clock_type::time_point now) {
        expired_lifecycle_kind = kind;
        expired_at = now;
        return lifecycle_expiry_result;
    }
};

struct order_sensitive_timer_session {
    bool recovery_checked{};
    std::vector<std::string_view> calls;

    [[nodiscard]] std::optional<flowq::quic::connection_recovery_timer> next_recovery_timer(clock_type::time_point) {
        calls.push_back("recovery");
        recovery_checked = true;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<flowq::quic::connection_lifecycle_timer> next_lifecycle_timer(clock_type::time_point) {
        calls.push_back("lifecycle");
        if (!recovery_checked) {
            return flowq::quic::connection_lifecycle_timer{
                flowq::quic::connection_lifecycle_timer_kind::idle,
                at(50ms)
            };
        }
        return flowq::quic::connection_lifecycle_timer{
            flowq::quic::connection_lifecycle_timer_kind::closing,
            at(5ms)
        };
    }

    [[nodiscard]] flowq::quic::connection_recovery_result on_recovery_timer(
        flowq::quic::packet_number_space space,
        clock_type::time_point) {
        return flowq::quic::connection_recovery_result{space, {}, {}};
    }

    [[nodiscard]] flowq::quic::session_send_result on_lifecycle_timer(
        flowq::quic::connection_lifecycle_timer_kind,
        clock_type::time_point) {
        return {};
    }
};

struct scheduler_receiver {
    std::optional<flowq::quic::quic_timer_scheduler_result>* result;
    bool* stopped{};

    friend void set_value(scheduler_receiver&& receiver, flowq::quic::quic_timer_scheduler_result result) noexcept {
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

TEST_CASE("QUIC timer scheduler completes immediately when session has no timers") {
    flowq::context context{};
    fake_quic_timer_session session{};
    std::optional<flowq::quic::quic_timer_scheduler_result> result;

    auto sender = flowq::quic::schedule_quic_timer(context, session, at(0ms));
    auto operation = sender.connect(scheduler_receiver{&result});

    operation.start();
    context.io_context().run();

    REQUIRE(result.has_value());
    REQUIRE(result->ok());
    CHECK_FALSE(result->fired.has_value());
    CHECK_FALSE(result->recovery_result.has_value());
    CHECK_FALSE(result->lifecycle_result.has_value());
}

TEST_CASE("QUIC timer scheduler selects earlier recovery timer") {
    flowq::context context{};
    fake_quic_timer_session session{};
    session.recovery_timer = flowq::quic::connection_recovery_timer{
        flowq::quic::packet_number_space::initial,
        flowq::quic::loss_timer_mode::pto,
        at(5ms)
    };
    session.lifecycle_timer = flowq::quic::connection_lifecycle_timer{
        flowq::quic::connection_lifecycle_timer_kind::idle,
        at(20ms)
    };
    std::optional<flowq::quic::quic_timer_scheduler_result> result;

    auto sender = flowq::quic::schedule_quic_timer(context, session, at(0ms));
    auto operation = sender.connect(scheduler_receiver{&result});

    operation.start();
    context.io_context().run();

    REQUIRE(result.has_value());
    REQUIRE(result->ok());
    REQUIRE(result->fired.has_value());
    CHECK(result->fired->kind == flowq::quic::quic_timer_kind::recovery);
    CHECK(result->fired->deadline == at(5ms));
    REQUIRE(result->fired->recovery.has_value());
    CHECK_FALSE(result->fired->lifecycle.has_value());
    REQUIRE(result->recovery_result.has_value());
    CHECK_FALSE(result->lifecycle_result.has_value());
    CHECK(result->recovery_result->newly_lost == std::vector<std::uint64_t>{7});
    REQUIRE(session.expired_recovery_space.has_value());
    CHECK(*session.expired_recovery_space == flowq::quic::packet_number_space::initial);
    CHECK_FALSE(session.expired_lifecycle_kind.has_value());
    REQUIRE(session.expired_at.has_value());
    CHECK(*session.expired_at == at(5ms));
}

TEST_CASE("QUIC timer scheduler selects earlier lifecycle timer") {
    flowq::context context{};
    fake_quic_timer_session session{};
    session.recovery_timer = flowq::quic::connection_recovery_timer{
        flowq::quic::packet_number_space::application,
        flowq::quic::loss_timer_mode::loss_time,
        at(30ms)
    };
    session.lifecycle_timer = flowq::quic::connection_lifecycle_timer{
        flowq::quic::connection_lifecycle_timer_kind::idle,
        at(5ms)
    };
    session.lifecycle_expiry_result.error = flowq::error{flowq::error_code::timeout, "idle timeout expired"};
    session.lifecycle_expiry_result.closes.push_back(flowq::quic::close_action{session.lifecycle_expiry_result.error});
    std::optional<flowq::quic::quic_timer_scheduler_result> result;

    auto sender = flowq::quic::schedule_quic_timer(context, session, at(0ms));
    auto operation = sender.connect(scheduler_receiver{&result});

    operation.start();
    context.io_context().run();

    REQUIRE(result.has_value());
    REQUIRE(result->ok());
    REQUIRE(result->fired.has_value());
    CHECK(result->fired->kind == flowq::quic::quic_timer_kind::lifecycle);
    CHECK(result->fired->deadline == at(5ms));
    CHECK_FALSE(result->fired->recovery.has_value());
    REQUIRE(result->fired->lifecycle.has_value());
    CHECK_FALSE(result->recovery_result.has_value());
    REQUIRE(result->lifecycle_result.has_value());
    CHECK_FALSE(result->lifecycle_result->ok());
    CHECK(result->lifecycle_result->error.code() == flowq::error_code::timeout);
    CHECK_FALSE(session.expired_recovery_space.has_value());
    REQUIRE(session.expired_lifecycle_kind.has_value());
    CHECK(*session.expired_lifecycle_kind == flowq::quic::connection_lifecycle_timer_kind::idle);
    REQUIRE(session.expired_at.has_value());
    CHECK(*session.expired_at == at(5ms));
}

TEST_CASE("QUIC timer scheduler favors lifecycle timer when deadlines tie") {
    flowq::context context{};
    fake_quic_timer_session session{};
    session.recovery_timer = flowq::quic::connection_recovery_timer{
        flowq::quic::packet_number_space::initial,
        flowq::quic::loss_timer_mode::pto,
        at(5ms)
    };
    session.lifecycle_timer = flowq::quic::connection_lifecycle_timer{
        flowq::quic::connection_lifecycle_timer_kind::idle,
        at(5ms)
    };
    std::optional<flowq::quic::quic_timer_scheduler_result> result;

    auto sender = flowq::quic::schedule_quic_timer(context, session, at(0ms));
    auto operation = sender.connect(scheduler_receiver{&result});

    operation.start();
    context.io_context().run();

    REQUIRE(result.has_value());
    REQUIRE(result->ok());
    REQUIRE(result->fired.has_value());
    CHECK(result->fired->kind == flowq::quic::quic_timer_kind::lifecycle);
    CHECK(result->fired->deadline == at(5ms));
    CHECK_FALSE(result->recovery_result.has_value());
    REQUIRE(result->lifecycle_result.has_value());
    CHECK_FALSE(session.expired_recovery_space.has_value());
    REQUIRE(session.expired_lifecycle_kind.has_value());
    CHECK(*session.expired_lifecycle_kind == flowq::quic::connection_lifecycle_timer_kind::idle);
}

TEST_CASE("QUIC timer scheduler observes recovery state before selecting lifecycle timer") {
    flowq::context context{};
    order_sensitive_timer_session session{};
    std::optional<flowq::quic::quic_timer_scheduler_result> result;

    auto sender = flowq::quic::schedule_quic_timer(context, session, at(0ms));
    auto operation = sender.connect(scheduler_receiver{&result});

    operation.start();
    context.io_context().run();

    REQUIRE(result.has_value());
    REQUIRE(result->fired.has_value());
    CHECK(session.calls == std::vector<std::string_view>{"recovery", "lifecycle"});
    CHECK(result->fired->kind == flowq::quic::quic_timer_kind::lifecycle);
    CHECK(result->fired->deadline == at(5ms));
    REQUIRE(result->fired->lifecycle.has_value());
    CHECK(result->fired->lifecycle->kind == flowq::quic::connection_lifecycle_timer_kind::closing);
}

TEST_CASE("QUIC timer scheduler cancellation maps to stopped") {
    flowq::context context{};
    fake_quic_timer_session session{};
    session.lifecycle_timer = flowq::quic::connection_lifecycle_timer{
        flowq::quic::connection_lifecycle_timer_kind::idle,
        clock_type::now() + 1h
    };
    std::optional<flowq::quic::quic_timer_scheduler_result> result;
    bool stopped = false;

    auto sender = flowq::quic::schedule_quic_timer(context, session, clock_type::now());
    auto operation = sender.connect(scheduler_receiver{&result, &stopped});

    operation.start();
    operation.cancel();
    context.io_context().run();

    CHECK(stopped);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("QUIC timer scheduler drives real session idle timeout close") {
    flowq::context context{};
    flowq::quic::plaintext_packet_protector protector{};
    auto config = make_config(
        cid({0x01}),
        cid({0x02}),
        flowq::endpoint{"server", 4433, "hq-interop"},
        protector);
    config.max_idle_timeout = 5ms;
    flowq::quic::session session{std::move(config)};
    std::optional<flowq::quic::quic_timer_scheduler_result> result;

    REQUIRE(session.append_stream_data(0, text("hello")).ok());
    REQUIRE(session.queue_stream_data({0}).ok());
    REQUIRE(session.flush(at(0ms)).ok());

    auto sender = flowq::quic::schedule_quic_timer(context, session, at(0ms));
    auto operation = sender.connect(scheduler_receiver{&result});

    operation.start();
    context.io_context().run();

    REQUIRE(result.has_value());
    REQUIRE(result->ok());
    REQUIRE(result->fired.has_value());
    CHECK(result->fired->kind == flowq::quic::quic_timer_kind::lifecycle);
    CHECK(result->fired->deadline == at(5ms));
    REQUIRE(result->lifecycle_result.has_value());
    CHECK_FALSE(result->lifecycle_result->ok());
    REQUIRE(result->lifecycle_result->closes.size() == 1);
    CHECK(result->lifecycle_result->error.code() == flowq::error_code::timeout);
}
