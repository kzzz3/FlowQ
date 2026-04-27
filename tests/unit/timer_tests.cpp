#include <flowq/asio/timer.hpp>
#include <flowq/context.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>

namespace {

struct timer_value_receiver {
    bool* completed;

    friend void set_value(timer_value_receiver&& receiver) noexcept {
        *receiver.completed = true;
    }

    friend void set_error(timer_value_receiver&&, flowq::error) noexcept {}
    friend void set_stopped(timer_value_receiver&&) noexcept {}
};

struct timer_stopped_receiver {
    bool* stopped;

    friend void set_value(timer_stopped_receiver&&) noexcept {}
    friend void set_error(timer_stopped_receiver&&, flowq::error) noexcept {}

    friend void set_stopped(timer_stopped_receiver&& receiver) noexcept {
        *receiver.stopped = true;
    }
};

} // namespace

TEST_CASE("timer sender completes after duration") {
    using namespace std::chrono_literals;

    flowq::context context{};
    bool completed = false;
    auto sender = flowq::asio::sleep_for(context, 0ms);
    auto operation = sender.connect(timer_value_receiver{&completed});

    operation.start();
    context.io_context().run();

    CHECK(completed);
}

TEST_CASE("timer sender maps cancellation to stopped") {
    using namespace std::chrono_literals;

    flowq::context context{};
    bool stopped = false;
    auto sender = flowq::asio::sleep_for(context, 1h);
    auto operation = sender.connect(timer_stopped_receiver{&stopped});

    operation.start();
    operation.cancel();
    context.io_context().run();

    CHECK(stopped);
}
