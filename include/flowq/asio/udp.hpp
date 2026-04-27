#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>

#include <asio/buffer.hpp>
#include <asio/error.hpp>
#include <asio/ip/udp.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace flowq::asio {

struct udp_datagram {
    flowq::buffer payload;
    ::asio::ip::udp::endpoint remote;
};

namespace detail {

[[nodiscard]] inline flowq::error udp_error(const ::asio::error_code& error_code) {
    return flowq::error{flowq::error_code::udp_error, error_code.message()};
}

template <class Receiver>
class udp_send_state : public std::enable_shared_from_this<udp_send_state<Receiver>> {
public:
    udp_send_state(
        ::asio::ip::udp::socket& socket,
        ::asio::ip::udp::endpoint remote,
        flowq::buffer payload,
        Receiver receiver)
        : socket_{&socket}, remote_{std::move(remote)}, payload_{std::move(payload)}, receiver_{std::move(receiver)} {}

    void start() {
        socket_->async_send_to(
            ::asio::buffer(payload_.data(), payload_.size()),
            remote_,
            [self = this->shared_from_this()](const ::asio::error_code& error_code, std::size_t bytes_sent) mutable {
                self->complete(error_code, bytes_sent);
            });
    }

    void cancel() {
        socket_->cancel();
    }

private:
    void complete(const ::asio::error_code& error_code, std::size_t bytes_sent) {
        if (!receiver_.has_value()) {
            return;
        }

        auto receiver = std::move(*receiver_);
        receiver_.reset();

        if (!error_code) {
            set_value(std::move(receiver), bytes_sent);
            return;
        }

        if (error_code == ::asio::error::operation_aborted) {
            set_stopped(std::move(receiver));
            return;
        }

        set_error(std::move(receiver), udp_error(error_code));
    }

    ::asio::ip::udp::socket* socket_;
    ::asio::ip::udp::endpoint remote_;
    flowq::buffer payload_;
    std::optional<Receiver> receiver_;
};

template <class Receiver>
class udp_receive_state : public std::enable_shared_from_this<udp_receive_state<Receiver>> {
public:
    udp_receive_state(::asio::ip::udp::socket& socket, std::size_t max_size, Receiver receiver)
        : socket_{&socket}, storage_(max_size), receiver_{std::move(receiver)} {}

    void start() {
        socket_->async_receive_from(
            ::asio::buffer(storage_.data(), storage_.size()),
            remote_,
            [self = this->shared_from_this()](const ::asio::error_code& error_code, std::size_t bytes_received) mutable {
                self->complete(error_code, bytes_received);
            });
    }

    void cancel() {
        socket_->cancel();
    }

private:
    void complete(const ::asio::error_code& error_code, std::size_t bytes_received) {
        if (!receiver_.has_value()) {
            return;
        }

        auto receiver = std::move(*receiver_);
        receiver_.reset();

        if (!error_code) {
            udp_datagram datagram{
                flowq::buffer{std::span<const std::byte>{storage_.data(), bytes_received}},
                std::move(remote_)
            };
            set_value(std::move(receiver), std::move(datagram));
            return;
        }

        if (error_code == ::asio::error::operation_aborted) {
            set_stopped(std::move(receiver));
            return;
        }

        set_error(std::move(receiver), udp_error(error_code));
    }

    ::asio::ip::udp::socket* socket_;
    ::asio::ip::udp::endpoint remote_{};
    std::vector<std::byte> storage_;
    std::optional<Receiver> receiver_;
};

template <class State>
class udp_operation {
public:
    explicit udp_operation(std::shared_ptr<State> state)
        : state_{std::move(state)} {}

    void start() {
        state_->start();
    }

    void cancel() {
        state_->cancel();
    }

private:
    std::shared_ptr<State> state_;
};

class udp_receive_sender {
public:
    udp_receive_sender(::asio::ip::udp::socket& socket, std::size_t max_size)
        : socket_{&socket}, max_size_{max_size} {}

    template <class Receiver>
    [[nodiscard]] auto connect(Receiver receiver) const {
        using receiver_type = std::decay_t<Receiver>;
        using state_type = udp_receive_state<receiver_type>;
        return udp_operation<state_type>{std::make_shared<state_type>(*socket_, max_size_, std::move(receiver))};
    }

private:
    ::asio::ip::udp::socket* socket_;
    std::size_t max_size_;
};

class udp_send_sender {
public:
    udp_send_sender(::asio::ip::udp::socket& socket, ::asio::ip::udp::endpoint remote, flowq::buffer payload)
        : socket_{&socket}, remote_{std::move(remote)}, payload_{std::move(payload)} {}

    template <class Receiver>
    [[nodiscard]] auto connect(Receiver receiver) const {
        using receiver_type = std::decay_t<Receiver>;
        using state_type = udp_send_state<receiver_type>;
        return udp_operation<state_type>{std::make_shared<state_type>(*socket_, remote_, payload_, std::move(receiver))};
    }

private:
    ::asio::ip::udp::socket* socket_;
    ::asio::ip::udp::endpoint remote_;
    flowq::buffer payload_;
};

} // namespace detail

[[nodiscard]] inline auto async_receive(::asio::ip::udp::socket& socket, std::size_t max_size) {
    return detail::udp_receive_sender{socket, max_size};
}

template <std::ranges::contiguous_range Range>
    requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<Range>>, std::byte>
[[nodiscard]] auto async_send_to(::asio::ip::udp::socket& socket, ::asio::ip::udp::endpoint remote, const Range& payload) {
    return detail::udp_send_sender{socket, std::move(remote), flowq::buffer{payload}};
}

[[nodiscard]] inline auto async_send_to(
    ::asio::ip::udp::socket& socket,
    ::asio::ip::udp::endpoint remote,
    std::span<const std::byte> payload) {
    return detail::udp_send_sender{socket, std::move(remote), flowq::buffer{payload}};
}

} // namespace flowq::asio
