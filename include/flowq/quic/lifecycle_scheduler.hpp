#pragma once

#include <flowq/context.hpp>
#include <flowq/error.hpp>
#include <flowq/quic/session.hpp>

#include <asio/error.hpp>
#include <asio/steady_timer.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace flowq::quic {

/// Result delivered after a QUIC lifecycle timer is scheduled and fired.
struct lifecycle_scheduler_result {
    std::optional<connection_lifecycle_timer> fired;
    std::optional<session_send_result> send_result;
    flowq::error error{};

    /// Return whether the scheduler operation completed without an error.
    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

namespace detail {

[[nodiscard]] inline flowq::error lifecycle_scheduler_error(const ::asio::error_code& error_code) {
    return flowq::error{flowq::error_code::internal_error, error_code.message()};
}

template <class Receiver>
class lifecycle_scheduler_operation {
public:
    explicit lifecycle_scheduler_operation(std::shared_ptr<Receiver> state)
        : state_{std::move(state)} {}

    void start() {
        state_->start();
    }

    void cancel() {
        state_->cancel();
    }

private:
    std::shared_ptr<Receiver> state_;
};

template <class Session, class Receiver>
class lifecycle_scheduler_state : public std::enable_shared_from_this<lifecycle_scheduler_state<Session, Receiver>> {
public:
    lifecycle_scheduler_state(flowq::context& context, Session& session, std::chrono::steady_clock::time_point now, Receiver receiver)
        : timer_{context.io_context()}, session_{&session}, now_{now}, receiver_{std::move(receiver)} {}

    void start() {
        auto selected = session_->next_lifecycle_timer(now_);
        if (!selected.has_value()) {
            complete_value(lifecycle_scheduler_result{});
            return;
        }

        fired_ = *selected;
        timer_.expires_at(fired_->deadline);
        timer_.async_wait([self = this->shared_from_this()](const ::asio::error_code& error_code) mutable {
            self->complete_wait(error_code);
        });
    }

    void cancel() {
        timer_.cancel();
    }

private:
    void complete_wait(const ::asio::error_code& error_code) {
        if (error_code) {
            complete_error(error_code);
            return;
        }

        lifecycle_scheduler_result result{};
        result.fired = fired_;
        if (fired_.has_value()) {
            result.send_result = session_->on_lifecycle_timer(fired_->kind, fired_->deadline);
        }
        complete_value(std::move(result));
    }

    void complete_value(lifecycle_scheduler_result result) {
        if (!receiver_.has_value()) {
            return;
        }
        auto receiver = std::move(*receiver_);
        receiver_.reset();
        set_value(std::move(receiver), std::move(result));
    }

    void complete_error(const ::asio::error_code& error_code) {
        if (!receiver_.has_value()) {
            return;
        }
        auto receiver = std::move(*receiver_);
        receiver_.reset();
        if (error_code == ::asio::error::operation_aborted) {
            set_stopped(std::move(receiver));
            return;
        }
        set_error(std::move(receiver), lifecycle_scheduler_error(error_code));
    }

    ::asio::steady_timer timer_;
    Session* session_;
    std::chrono::steady_clock::time_point now_;
    std::optional<Receiver> receiver_;
    std::optional<connection_lifecycle_timer> fired_;
};

template <class Session>
class lifecycle_scheduler_sender {
public:
    lifecycle_scheduler_sender(flowq::context& context, Session& session, std::chrono::steady_clock::time_point now)
        : context_{&context}, session_{&session}, now_{now} {}

    template <class Receiver>
    [[nodiscard]] auto connect(Receiver receiver) const {
        using receiver_type = std::decay_t<Receiver>;
        using state_type = lifecycle_scheduler_state<Session, receiver_type>;
        return lifecycle_scheduler_operation<state_type>{std::make_shared<state_type>(*context_, *session_, now_, std::move(receiver))};
    }

private:
    flowq::context* context_;
    Session* session_;
    std::chrono::steady_clock::time_point now_;
};

} // namespace detail

/// Create a sender that waits for the next lifecycle timer and dispatches it to the session.
template <class Session>
[[nodiscard]] auto schedule_lifecycle(flowq::context& context, Session& session, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
    return detail::lifecycle_scheduler_sender<Session>{context, session, now};
}

} // namespace flowq::quic
