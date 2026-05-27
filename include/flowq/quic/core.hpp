#pragma once

#include <flowq/buffer.hpp>
#include <flowq/endpoint.hpp>
#include <flowq/error.hpp>

#include <chrono>
#include <utility>
#include <variant>
#include <vector>

namespace flowq::quic {

/// Timer identifiers for protocol-level scheduling.
enum class timer_id {
    idle_timeout,
    loss_detection,
    pacing
};

/// Input: datagram received from the network (delivered by UDP layer).
struct inbound_datagram {
    flowq::buffer payload;
    flowq::endpoint peer;
};

/// Output: datagram to send to the network (emitted by protocol core).
struct outbound_datagram {
    flowq::buffer payload;
    flowq::endpoint peer;
};

/// Action: datagram received and processed by the protocol core.
struct received_datagram {
    flowq::buffer payload;
    flowq::endpoint peer;
};

/// Action: request to arm a timer with a specific delay.
struct arm_timer {
    timer_id id{};
    std::chrono::steady_clock::duration delay{};
};

/// Input: timer event fired by the scheduling layer.
struct timer_fired {
    timer_id id{};
};

/// Action: timer expired, delivered back to the protocol core.
struct timer_expired {
    timer_id id{};
};

/// Action: connection close with error.
struct close_action {
    flowq::error error;
};

/// Protocol action variant: all possible side effects from protocol processing.
using action = std::variant<outbound_datagram, received_datagram, arm_timer, timer_expired, close_action>;

/// Deterministic protocol core: processes inputs and emits actions.
/// Input types: inbound_datagram, timer_fired
/// Output types: outbound_datagram, received_datagram, arm_timer, timer_expired, close_action
class core {
public:
    /// Process an inbound datagram, converting it to a received_datagram action.
    void on_datagram(inbound_datagram datagram) {
        actions_.emplace_back(received_datagram{std::move(datagram.payload), std::move(datagram.peer)});
    }

    /// Process a timer fired event, converting it to a timer_expired action.
    void on_timer(timer_fired timer) {
        actions_.emplace_back(timer_expired{timer.id});
    }

    /// Request a timer to be armed with a specific delay.
    void request_timer(timer_id id, std::chrono::steady_clock::duration delay) {
        actions_.emplace_back(arm_timer{id, delay});
    }

    /// Close the connection with an error.
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
