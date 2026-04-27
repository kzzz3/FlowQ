#pragma once

#include <flowq/context.hpp>
#include <flowq/error.hpp>

#include <asio/error.hpp>
#include <asio/steady_timer.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace flowq::asio {

namespace detail {

[[nodiscard]] inline flowq::error timer_error(const ::asio::error_code& error_code) {
    return flowq::error{flowq::error_code::internal_error, error_code.message()};
}

template <class Receiver>
class sleep_for_state : public std::enable_shared_from_this<sleep_for_state<Receiver>> {
public:
    template <class Duration>
    sleep_for_state(flowq::context& context, Duration duration, Receiver receiver)
        : timer_{context.io_context(), duration}, receiver_{std::move(receiver)} {}

    void start() {
        timer_.async_wait([self = this->shared_from_this()](const ::asio::error_code& error_code) mutable {
            self->complete(error_code);
        });
    }

    void cancel() {
        timer_.cancel();
    }

private:
    void complete(const ::asio::error_code& error_code) {
        if (!receiver_.has_value()) {
            return;
        }

        auto receiver = std::move(*receiver_);
        receiver_.reset();

        if (!error_code) {
            set_value(std::move(receiver));
            return;
        }

        if (error_code == ::asio::error::operation_aborted) {
            set_stopped(std::move(receiver));
            return;
        }

        set_error(std::move(receiver), timer_error(error_code));
    }

    ::asio::steady_timer timer_;
    std::optional<Receiver> receiver_;
};

template <class Receiver>
class sleep_for_operation {
public:
    explicit sleep_for_operation(std::shared_ptr<sleep_for_state<Receiver>> state)
        : state_{std::move(state)} {}

    void start() {
        state_->start();
    }

    void cancel() {
        state_->cancel();
    }

private:
    std::shared_ptr<sleep_for_state<Receiver>> state_;
};

template <class Rep, class Period>
class sleep_for_sender {
public:
    using duration_type = std::chrono::duration<Rep, Period>;

    sleep_for_sender(flowq::context& context, duration_type duration)
        : context_{&context}, duration_{duration} {}

    template <class Receiver>
    [[nodiscard]] auto connect(Receiver receiver) const {
        using receiver_type = std::decay_t<Receiver>;
        auto shared_state = std::make_shared<sleep_for_state<receiver_type>>(*context_, duration_, std::move(receiver));
        return sleep_for_operation<receiver_type>{std::move(shared_state)};
    }

private:
    flowq::context* context_;
    duration_type duration_;
};

} // namespace detail

template <class Rep, class Period>
[[nodiscard]] auto sleep_for(flowq::context& context, std::chrono::duration<Rep, Period> duration) {
    return detail::sleep_for_sender<Rep, Period>{context, duration};
}

} // namespace flowq::asio
