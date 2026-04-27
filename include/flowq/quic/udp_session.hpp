#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>
#include <flowq/quic/session.hpp>

#include <asio/buffer.hpp>
#include <asio/error.hpp>
#include <asio/ip/udp.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace flowq::quic {

struct udp_session_config {
    session_config quic;
    ::asio::ip::udp::endpoint peer;
    std::size_t receive_max_size{1200};
};

struct udp_session_send_result {
    session_send_result session_result;
    std::size_t bytes_sent{};
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok() && session_result.ok();
    }
};

struct udp_session_receive_result {
    ::asio::ip::udp::endpoint remote;
    session_receive_result session_result;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok() && session_result.ok();
    }
};

namespace detail {

[[nodiscard]] inline flowq::error udp_session_error(const ::asio::error_code& error_code) {
    return flowq::error{flowq::error_code::udp_error, error_code.message()};
}

[[nodiscard]] inline flowq::endpoint to_flowq_endpoint(const ::asio::ip::udp::endpoint& endpoint, std::string alpn) {
    return flowq::endpoint{endpoint.address().to_string(), endpoint.port(), std::move(alpn)};
}

template <class State>
class udp_session_operation {
public:
    explicit udp_session_operation(std::shared_ptr<State> state)
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

template <class Receiver>
class udp_session_send_state : public std::enable_shared_from_this<udp_session_send_state<Receiver>> {
public:
    udp_session_send_state(
        ::asio::ip::udp::socket& socket,
        session& quic,
        ::asio::ip::udp::endpoint peer,
        std::chrono::steady_clock::time_point sent_at,
        Receiver receiver)
        : socket_{&socket}, quic_{&quic}, peer_{std::move(peer)}, sent_at_{sent_at}, receiver_{std::move(receiver)} {}

    void start() {
        result_.session_result = quic_->flush(sent_at_);
        if (!result_.session_result.ok() || result_.session_result.datagrams.empty()) {
            complete_value();
            return;
        }
        send_next();
    }

    void cancel() {
        socket_->cancel();
    }

private:
    void send_next() {
        if (next_datagram_ == result_.session_result.datagrams.size()) {
            complete_value();
            return;
        }

        const auto& payload = result_.session_result.datagrams[next_datagram_].payload;
        socket_->async_send_to(
            ::asio::buffer(payload.data(), payload.size()),
            peer_,
            [self = this->shared_from_this()](const ::asio::error_code& error_code, std::size_t bytes_sent) mutable {
                self->complete_send(error_code, bytes_sent);
            });
    }

    void complete_send(const ::asio::error_code& error_code, std::size_t bytes_sent) {
        if (error_code) {
            complete_error(error_code);
            return;
        }
        result_.bytes_sent += bytes_sent;
        ++next_datagram_;
        send_next();
    }

    void complete_value() {
        if (!receiver_.has_value()) {
            return;
        }
        auto receiver = std::move(*receiver_);
        receiver_.reset();
        set_value(std::move(receiver), std::move(result_));
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
        set_error(std::move(receiver), udp_session_error(error_code));
    }

    ::asio::ip::udp::socket* socket_;
    session* quic_;
    ::asio::ip::udp::endpoint peer_;
    std::chrono::steady_clock::time_point sent_at_;
    std::optional<Receiver> receiver_;
    udp_session_send_result result_{};
    std::size_t next_datagram_{};
};

template <class Receiver>
class udp_session_receive_state : public std::enable_shared_from_this<udp_session_receive_state<Receiver>> {
public:
    udp_session_receive_state(
        ::asio::ip::udp::socket& socket,
        session& quic,
        std::size_t receive_max_size,
        std::string alpn,
        Receiver receiver)
        : socket_{&socket}, quic_{&quic}, storage_(receive_max_size), alpn_{std::move(alpn)}, receiver_{std::move(receiver)} {}

    void start() {
        socket_->async_receive_from(
            ::asio::buffer(storage_.data(), storage_.size()),
            remote_,
            [self = this->shared_from_this()](const ::asio::error_code& error_code, std::size_t bytes_received) mutable {
                self->complete_receive(error_code, bytes_received);
            });
    }

    void cancel() {
        socket_->cancel();
    }

private:
    void complete_receive(const ::asio::error_code& error_code, std::size_t bytes_received) {
        if (error_code) {
            complete_error(error_code);
            return;
        }

        auto payload = flowq::buffer{std::span<const std::byte>{storage_.data(), bytes_received}};
        auto peer = to_flowq_endpoint(remote_, alpn_);
        udp_session_receive_result result{
            remote_,
            quic_->on_datagram(inbound_datagram{std::move(payload), std::move(peer)}),
            {}
        };
        complete_value(std::move(result));
    }

    void complete_value(udp_session_receive_result result) {
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
        set_error(std::move(receiver), udp_session_error(error_code));
    }

    ::asio::ip::udp::socket* socket_;
    session* quic_;
    ::asio::ip::udp::endpoint remote_{};
    std::vector<std::byte> storage_;
    std::string alpn_;
    std::optional<Receiver> receiver_;
};

class udp_session_send_sender {
public:
    udp_session_send_sender(
        ::asio::ip::udp::socket& socket,
        session& quic,
        ::asio::ip::udp::endpoint peer,
        std::chrono::steady_clock::time_point sent_at)
        : socket_{&socket}, quic_{&quic}, peer_{std::move(peer)}, sent_at_{sent_at} {}

    template <class Receiver>
    [[nodiscard]] auto connect(Receiver receiver) const {
        using receiver_type = std::decay_t<Receiver>;
        using state_type = udp_session_send_state<receiver_type>;
        return udp_session_operation<state_type>{std::make_shared<state_type>(*socket_, *quic_, peer_, sent_at_, std::move(receiver))};
    }

private:
    ::asio::ip::udp::socket* socket_;
    session* quic_;
    ::asio::ip::udp::endpoint peer_;
    std::chrono::steady_clock::time_point sent_at_;
};

class udp_session_receive_sender {
public:
    udp_session_receive_sender(::asio::ip::udp::socket& socket, session& quic, std::size_t receive_max_size, std::string alpn)
        : socket_{&socket}, quic_{&quic}, receive_max_size_{receive_max_size}, alpn_{std::move(alpn)} {}

    template <class Receiver>
    [[nodiscard]] auto connect(Receiver receiver) const {
        using receiver_type = std::decay_t<Receiver>;
        using state_type = udp_session_receive_state<receiver_type>;
        return udp_session_operation<state_type>{std::make_shared<state_type>(*socket_, *quic_, receive_max_size_, alpn_, std::move(receiver))};
    }

private:
    ::asio::ip::udp::socket* socket_;
    session* quic_;
    std::size_t receive_max_size_;
    std::string alpn_;
};

} // namespace detail

class udp_session {
public:
    udp_session(::asio::ip::udp::socket& socket, udp_session_config config)
        : socket_{&socket},
          peer_{std::move(config.peer)},
          receive_max_size_{config.receive_max_size},
          receive_peer_alpn_{config.quic.peer.alpn},
          quic_{std::move(config.quic)} {}

    [[nodiscard]] ::asio::ip::udp::socket& socket() noexcept {
        return *socket_;
    }

    [[nodiscard]] const ::asio::ip::udp::endpoint& peer_endpoint() const noexcept {
        return peer_;
    }

    [[nodiscard]] std::size_t receive_max_size() const noexcept {
        return receive_max_size_;
    }

    [[nodiscard]] stream_operation_result append_stream_data(std::uint64_t stream_id, const flowq::buffer& data) {
        return quic_.append_stream_data(stream_id, data);
    }

    [[nodiscard]] session_send_result queue_stream_data(std::initializer_list<std::uint64_t> stream_ids) {
        return quic_.queue_stream_data(stream_ids);
    }

    [[nodiscard]] auto send_pending(std::chrono::steady_clock::time_point sent_at = std::chrono::steady_clock::now()) {
        return detail::udp_session_send_sender{*socket_, quic_, peer_, sent_at};
    }

    [[nodiscard]] auto async_receive_once() {
        return detail::udp_session_receive_sender{*socket_, quic_, receive_max_size_, receive_peer_alpn_};
    }

private:
    ::asio::ip::udp::socket* socket_;
    ::asio::ip::udp::endpoint peer_;
    std::size_t receive_max_size_;
    std::string receive_peer_alpn_;
    session quic_;
};

} // namespace flowq::quic
