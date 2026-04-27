#include <flowq/quic/core.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <variant>

namespace {

flowq::buffer bytes(std::initializer_list<std::byte> payload) {
    return flowq::buffer{std::span<const std::byte>{payload.begin(), payload.size()}};
}

} // namespace

TEST_CASE("protocol core records inbound datagram as deterministic action") {
    flowq::quic::core core{};
    flowq::endpoint peer{"127.0.0.1", 4433, "h3"};

    core.on_datagram(flowq::quic::inbound_datagram{bytes({std::byte{0x01}, std::byte{0x02}}), peer});

    auto actions = core.drain_actions();

    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::received_datagram>(actions[0]));

    const auto& action = std::get<flowq::quic::received_datagram>(actions[0]);
    CHECK(action.peer.host == "127.0.0.1");
    CHECK(action.peer.port == 4433);
    CHECK(action.payload.size() == 2);
    CHECK(action.payload.data()[0] == std::byte{0x01});
    CHECK(action.payload.data()[1] == std::byte{0x02});
}

TEST_CASE("protocol core drains actions in FIFO order") {
    using namespace std::chrono_literals;

    flowq::quic::core core{};
    core.request_timer(flowq::quic::timer_id::idle_timeout, 30s);
    core.close(flowq::error{flowq::error_code::protocol_error, "bad packet"});

    auto actions = core.drain_actions();

    REQUIRE(actions.size() == 2);
    CHECK(std::holds_alternative<flowq::quic::arm_timer>(actions[0]));
    CHECK(std::holds_alternative<flowq::quic::close_action>(actions[1]));

    const auto& timer = std::get<flowq::quic::arm_timer>(actions[0]);
    CHECK(timer.id == flowq::quic::timer_id::idle_timeout);
    CHECK(timer.delay == 30s);

    const auto& close = std::get<flowq::quic::close_action>(actions[1]);
    CHECK(close.error.code() == flowq::error_code::protocol_error);
    CHECK(close.error.message() == "bad packet");
}

TEST_CASE("protocol core records timer fired input as deterministic action") {
    flowq::quic::core core{};

    core.on_timer(flowq::quic::timer_fired{flowq::quic::timer_id::loss_detection});

    auto actions = core.drain_actions();

    REQUIRE(actions.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::timer_expired>(actions[0]));
    CHECK(std::get<flowq::quic::timer_expired>(actions[0]).id == flowq::quic::timer_id::loss_detection);
}

TEST_CASE("protocol core drain empties action queue") {
    flowq::quic::core core{};
    core.close(flowq::error{flowq::error_code::connection_closed, "closed"});

    CHECK(core.has_actions());
    auto actions = core.drain_actions();

    CHECK(actions.size() == 1);
    CHECK_FALSE(core.has_actions());
    CHECK(core.drain_actions().empty());
}
