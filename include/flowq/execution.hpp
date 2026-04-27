#pragma once

#include <flowq/error.hpp>

#include <type_traits>
#include <utility>

namespace flowq::execution {

namespace detail {

template <class Receiver, class Value>
class just_operation {
public:
    just_operation(Receiver receiver, Value value)
        : receiver_{std::move(receiver)}, value_{std::move(value)} {}

    void start() noexcept(noexcept(set_value(std::move(receiver_), std::move(value_)))) {
        set_value(std::move(receiver_), std::move(value_));
    }

private:
    Receiver receiver_;
    Value value_;
};

template <class Receiver>
class stopped_operation {
public:
    explicit stopped_operation(Receiver receiver)
        : receiver_{std::move(receiver)} {}

    void start() noexcept(noexcept(set_stopped(std::move(receiver_)))) {
        set_stopped(std::move(receiver_));
    }

private:
    Receiver receiver_;
};

template <class Receiver>
class error_operation {
public:
    error_operation(Receiver receiver, flowq::error error)
        : receiver_{std::move(receiver)}, error_{std::move(error)} {}

    void start() noexcept(noexcept(set_error(std::move(receiver_), std::move(error_)))) {
        set_error(std::move(receiver_), std::move(error_));
    }

private:
    Receiver receiver_;
    flowq::error error_;
};

template <class Value>
class just_sender {
public:
    explicit just_sender(Value value)
        : value_{std::move(value)} {}

    template <class Receiver>
    [[nodiscard]] auto connect(Receiver receiver) & {
        return just_operation<std::decay_t<Receiver>, Value>{std::move(receiver), value_};
    }

    template <class Receiver>
    [[nodiscard]] auto connect(Receiver receiver) && {
        return just_operation<std::decay_t<Receiver>, Value>{std::move(receiver), std::move(value_)};
    }

private:
    Value value_;
};

class stopped_sender {
public:
    template <class Receiver>
    [[nodiscard]] auto connect(Receiver receiver) const {
        return stopped_operation<std::decay_t<Receiver>>{std::move(receiver)};
    }
};

class error_sender {
public:
    explicit error_sender(flowq::error error)
        : error_{std::move(error)} {}

    template <class Receiver>
    [[nodiscard]] auto connect(Receiver receiver) & {
        return error_operation<std::decay_t<Receiver>>{std::move(receiver), error_};
    }

    template <class Receiver>
    [[nodiscard]] auto connect(Receiver receiver) && {
        return error_operation<std::decay_t<Receiver>>{std::move(receiver), std::move(error_)};
    }

private:
    flowq::error error_;
};

} // namespace detail

template <class Value>
[[nodiscard]] auto just(Value&& value) {
    return detail::just_sender<std::decay_t<Value>>{std::forward<Value>(value)};
}

[[nodiscard]] inline auto stopped() noexcept {
    return detail::stopped_sender{};
}

[[nodiscard]] inline auto error(flowq::error error) {
    return detail::error_sender{std::move(error)};
}

} // namespace flowq::execution
