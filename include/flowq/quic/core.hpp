#pragma once

#include <flowq/buffer.hpp>
#include <flowq/endpoint.hpp>
#include <flowq/error.hpp>

#include <chrono>
#include <utility>
#include <variant>
#include <vector>

namespace flowq::quic {

enum class timer_id {
    idle_timeout,
    loss_detection,
    pacing
};

struct inbound_datagram {
    flowq::buffer payload;
    flowq::endpoint peer;
};

struct outbound_datagram {
    flowq::buffer payload;
    flowq::endpoint peer;
};

struct received_datagram {
    flowq::buffer payload;
    flowq::endpoint peer;
};

struct arm_timer {
    timer_id id{};
    std::chrono::steady_clock::duration delay{};
};

struct timer_fired {
    timer_id id{};
};

struct timer_expired {
    timer_id id{};
};

struct close_action {
    flowq::error error;
};

using action = std::variant<outbound_datagram, received_datagram, arm_timer, timer_expired, close_action>;

class core {
public:
    void on_datagram(inbound_datagram datagram) {
        actions_.emplace_back(received_datagram{std::move(datagram.payload), std::move(datagram.peer)});
    }

    void on_timer(timer_fired timer) {
        actions_.emplace_back(timer_expired{timer.id});
    }

    void request_timer(timer_id id, std::chrono::steady_clock::duration delay) {
        actions_.emplace_back(arm_timer{id, delay});
    }

    void close(flowq::error error) {
        actions_.emplace_back(close_action{std::move(error)});
    }

    [[nodiscard]] bool has_actions() const noexcept {
        return !actions_.empty();
    }

    [[nodiscard]] std::vector<action> drain_actions() {
        auto drained = std::move(actions_);
        actions_.clear();
        return drained;
    }

private:
    std::vector<action> actions_{};
};

} // namespace flowq::quic
