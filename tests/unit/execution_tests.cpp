#include <flowq/execution.hpp>

#include <catch2/catch_test_macros.hpp>

#include <optional>

namespace {

struct value_receiver {
    std::optional<int>* value;

    friend void set_value(value_receiver&& receiver, int result) noexcept {
        receiver.value->emplace(result);
    }

    friend void set_error(value_receiver&&, flowq::error) noexcept {}
    friend void set_stopped(value_receiver&&) noexcept {}
};

struct stopped_receiver {
    bool* stopped;

    friend void set_value(stopped_receiver&&) noexcept {}
    friend void set_error(stopped_receiver&&, flowq::error) noexcept {}

    friend void set_stopped(stopped_receiver&& receiver) noexcept {
        *receiver.stopped = true;
    }
};

struct error_receiver {
    std::optional<flowq::error>* error;

    friend void set_value(error_receiver&&) noexcept {}

    friend void set_error(error_receiver&& receiver, flowq::error err) noexcept {
        receiver.error->emplace(std::move(err));
    }

    friend void set_stopped(error_receiver&&) noexcept {}
};

} // namespace

TEST_CASE("value sender completes receiver once when started") {
    std::optional<int> value;
    auto sender = flowq::execution::just(42);
    auto operation = sender.connect(value_receiver{&value});

    operation.start();

    REQUIRE(value.has_value());
    CHECK(*value == 42);
}

TEST_CASE("stopped sender maps cancellation to set_stopped") {
    bool stopped = false;
    auto sender = flowq::execution::stopped();
    auto operation = sender.connect(stopped_receiver{&stopped});

    operation.start();

    CHECK(stopped);
}

TEST_CASE("error sender completes receiver with flowq error") {
    std::optional<flowq::error> error;
    auto sender = flowq::execution::error(flowq::error{flowq::error_code::udp_error, "send failed"});
    auto operation = sender.connect(error_receiver{&error});

    operation.start();

    REQUIRE(error.has_value());
    CHECK(error->code() == flowq::error_code::udp_error);
    CHECK(error->message() == "send failed");
}
