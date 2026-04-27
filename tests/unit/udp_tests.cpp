#include <flowq/asio/udp.hpp>
#include <flowq/context.hpp>

#include <catch2/catch_test_macros.hpp>

#include <asio/ip/udp.hpp>

#include <array>
#include <cstddef>
#include <optional>

namespace {

using udp = ::asio::ip::udp;

struct receive_receiver {
    std::optional<flowq::asio::udp_datagram>* datagram;

    friend void set_value(receive_receiver&& receiver, flowq::asio::udp_datagram datagram) noexcept {
        receiver.datagram->emplace(std::move(datagram));
    }

    friend void set_error(receive_receiver&&, flowq::error) noexcept {}
    friend void set_stopped(receive_receiver&&) noexcept {}
};

struct send_receiver {
    std::optional<std::size_t>* bytes_sent;

    friend void set_value(send_receiver&& receiver, std::size_t bytes_sent) noexcept {
        receiver.bytes_sent->emplace(bytes_sent);
    }

    friend void set_error(send_receiver&&, flowq::error) noexcept {}
    friend void set_stopped(send_receiver&&) noexcept {}
};

struct receive_stopped_receiver {
    bool* stopped;

    friend void set_value(receive_stopped_receiver&&, flowq::asio::udp_datagram) noexcept {}
    friend void set_error(receive_stopped_receiver&&, flowq::error) noexcept {}

    friend void set_stopped(receive_stopped_receiver&& receiver) noexcept {
        *receiver.stopped = true;
    }
};

} // namespace

TEST_CASE("UDP send and receive complete over loopback") {
    flowq::context context{};
    udp::socket receiver_socket{context.io_context(), udp::endpoint{udp::v4(), 0}};
    udp::socket sender_socket{context.io_context(), udp::endpoint{udp::v4(), 0}};

    std::optional<flowq::asio::udp_datagram> received;
    std::optional<std::size_t> sent;
    std::array<std::byte, 4> payload{std::byte{'p'}, std::byte{'i'}, std::byte{'n'}, std::byte{'g'}};

    auto receive_sender = flowq::asio::async_receive(receiver_socket, 128);
    auto receive_operation = receive_sender.connect(receive_receiver{&received});
    udp::endpoint receiver_endpoint{::asio::ip::address_v4::loopback(), receiver_socket.local_endpoint().port()};
    auto send_sender = flowq::asio::async_send_to(sender_socket, receiver_endpoint, payload);
    auto send_operation = send_sender.connect(send_receiver{&sent});

    receive_operation.start();
    send_operation.start();
    context.io_context().run();

    REQUIRE(sent.has_value());
    CHECK(*sent == payload.size());
    REQUIRE(received.has_value());
    CHECK(received->payload.size() == payload.size());
    CHECK(received->payload.data()[0] == std::byte{'p'});
    CHECK(received->payload.data()[1] == std::byte{'i'});
    CHECK(received->payload.data()[2] == std::byte{'n'});
    CHECK(received->payload.data()[3] == std::byte{'g'});
    CHECK(received->remote.port() == sender_socket.local_endpoint().port());
}

TEST_CASE("UDP receive maps cancellation to stopped") {
    flowq::context context{};
    udp::socket receiver_socket{context.io_context(), udp::endpoint{udp::v4(), 0}};
    bool stopped = false;

    auto receive_sender = flowq::asio::async_receive(receiver_socket, 128);
    auto receive_operation = receive_sender.connect(receive_stopped_receiver{&stopped});

    receive_operation.start();
    receive_operation.cancel();
    context.io_context().run();

    CHECK(stopped);
}
