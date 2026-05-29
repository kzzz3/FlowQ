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

/// Identifies which QUIC timer family was selected by the unified scheduler.
enum class quic_timer_kind {
    recovery,
    lifecycle
};

/// Selected QUIC timer with its deadline and family-specific payload.
struct quic_timer {
    quic_timer_kind kind{quic_timer_kind::recovery};
    std::chrono::steady_clock::time_point deadline{};
    std::optional<connection_recovery_timer> recovery;
    std::optional<connection_lifecycle_timer> lifecycle;
};

/// Result delivered after the unified QUIC timer scheduler fires.
struct quic_timer_scheduler_result {
    std::optional<quic_timer> fired;
    std::optional<connection_recovery_result> recovery_result;
    std::optional<session_send_result> lifecycle_result;
    flowq::error error{};

    /// Return whether the scheduler operation completed without an error.
    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

namespace detail {

[[nodiscard]] inline flowq::error quic_timer_scheduler_error(const ::asio::error_code& error_code) {
    return flowq::error{flowq::error_code::internal_error, error_code.message()};
}

[[nodiscard]] inline std::optional<quic_timer> select_quic_timer(
    const std::optional<connection_recovery_timer>& recovery,
    const std::optional<connection_lifecycle_timer>& lifecycle) {
    if (!recovery.has_value() && !lifecycle.has_value()) {
        return std::nullopt;
    }
    if (lifecycle.has_value() && (!recovery.has_value() || lifecycle->deadline <= recovery->deadline)) {
        return quic_timer{quic_timer_kind::lifecycle, lifecycle->deadline, std::nullopt, lifecycle};
    }
    return quic_timer{quic_timer_kind::recovery, recovery->deadline, recovery, std::nullopt};
}

template <class Receiver>
class quic_timer_scheduler_operation {
public:
    explicit quic_timer_scheduler_operation(std::shared_ptr<Receiver> state)
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
class quic_timer_scheduler_state : public std::enable_shared_from_this<quic_timer_scheduler_state<Session, Receiver>> {
public:
    quic_timer_scheduler_state(flowq::context& context, Session& session, std::chrono::steady_clock::time_point now, Receiver receiver)
        : timer_{context.io_context()}, session_{&session}, now_{now}, receiver_{std::move(receiver)} {}

    void start() {
        auto recovery = session_->next_recovery_timer(now_);
        auto lifecycle = session_->next_lifecycle_timer(now_);
        fired_ = select_quic_timer(recovery, lifecycle);
        if (!fired_.has_value()) {
            complete_value(quic_timer_scheduler_result{});
            return;
        }

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

        quic_timer_scheduler_result result{};
        result.fired = fired_;
        if (fired_.has_value() && fired_->recovery.has_value()) {
            result.recovery_result = session_->on_recovery_timer(fired_->recovery->space, fired_->deadline);
        } else if (fired_.has_value() && fired_->lifecycle.has_value()) {
            result.lifecycle_result = session_->on_lifecycle_timer(fired_->lifecycle->kind, fired_->deadline);
        }
        complete_value(std::move(result));
    }

    void complete_value(quic_timer_scheduler_result result) {
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
        set_error(std::move(receiver), quic_timer_scheduler_error(error_code));
    }

    ::asio::steady_timer timer_;
    Session* session_;
    std::chrono::steady_clock::time_point now_;
    std::optional<Receiver> receiver_;
    std::optional<quic_timer> fired_;
};

template <class Session>
class quic_timer_scheduler_sender {
public:
    quic_timer_scheduler_sender(flowq::context& context, Session& session, std::chrono::steady_clock::time_point now)
        : context_{&context}, session_{&session}, now_{now} {}

    template <class Receiver>
    [[nodiscard]] auto connect(Receiver receiver) const {
        using receiver_type = std::decay_t<Receiver>;
        using state_type = quic_timer_scheduler_state<Session, receiver_type>;
        return quic_timer_scheduler_operation<state_type>{std::make_shared<state_type>(*context_, *session_, now_, std::move(receiver))};
    }

private:
    flowq::context* context_;
    Session* session_;
    std::chrono::steady_clock::time_point now_;
};

} // namespace detail

/// Create a sender that waits for the earliest recovery or lifecycle timer and dispatches it to the session.
template <class Session>
[[nodiscard]] auto schedule_quic_timer(flowq::context& context, Session& session, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
    return detail::quic_timer_scheduler_sender<Session>{context, session, now};
}

} // namespace flowq::quic
