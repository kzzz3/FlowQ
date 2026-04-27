#include <flowq/context.hpp>
#include <flowq/quic/udp_session.hpp>

#include <catch2/catch_test_macros.hpp>

#include <asio/ip/address_v4.hpp>
#include <asio/ip/udp.hpp>

#include <chrono>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using clock_type = std::chrono::steady_clock;
using udp = ::asio::ip::udp;

clock_type::time_point at(std::chrono::milliseconds offset) {
    return clock_type::time_point{offset};
}

std::vector<std::byte> bytes(std::initializer_list<unsigned int> values) {
    std::vector<std::byte> output;
    output.reserve(values.size());
    for (auto value : values) {
        output.push_back(static_cast<std::byte>(value & 0xffU));
    }
    return output;
}

flowq::quic::connection_id cid(std::initializer_list<unsigned int> values) {
    return flowq::quic::connection_id{flowq::buffer{bytes(values)}};
}

flowq::buffer text(std::string value) {
    std::vector<std::byte> output;
    output.reserve(value.size());
    for (auto character : value) {
        output.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return flowq::buffer{output};
}

std::string as_string(const flowq::buffer& buffer) {
    std::string output;
    output.reserve(buffer.size());
    for (std::size_t index = 0; index < buffer.size(); ++index) {
        output.push_back(static_cast<char>(buffer.data()[index]));
    }
    return output;
}

flowq::quic::session_config make_config(
    flowq::quic::connection_id local,
    flowq::quic::connection_id remote,
    flowq::endpoint peer,
    const flowq::quic::packet_protector& protector) {
    flowq::quic::session_config config{};
    config.role = flowq::quic::connection_role::client;
    config.local_connection_id = std::move(local);
    config.remote_connection_id = std::move(remote);
    config.peer = std::move(peer);
    config.initial_protector = &protector;
    config.handshake_protector = &protector;
    config.application_protector = &protector;
    return config;
}

struct send_receiver {
    std::optional<flowq::quic::udp_session_send_result>* result;

    friend void set_value(send_receiver&& receiver, flowq::quic::udp_session_send_result result) noexcept {
        receiver.result->emplace(std::move(result));
    }

    friend void set_error(send_receiver&&, flowq::error) noexcept {}
    friend void set_stopped(send_receiver&&) noexcept {}
};

struct receive_receiver {
    std::optional<flowq::quic::udp_session_receive_result>* result;

    friend void set_value(receive_receiver&& receiver, flowq::quic::udp_session_receive_result result) noexcept {
        receiver.result->emplace(std::move(result));
    }

    friend void set_error(receive_receiver&&, flowq::error) noexcept {}
    friend void set_stopped(receive_receiver&&) noexcept {}
};

} // namespace

TEST_CASE("UDP session binds an externally owned socket without taking ownership") {
    flowq::context context{};
    udp::socket socket{context.io_context(), udp::endpoint{udp::v4(), 0}};
    flowq::quic::plaintext_packet_protector protector{};
    udp::endpoint peer{::asio::ip::address_v4::loopback(), 4433};

    flowq::quic::udp_session adapter{socket, flowq::quic::udp_session_config{
        make_config(cid({0x01}), cid({0x02}), flowq::endpoint{"127.0.0.1", 4433, "hq-interop"}, protector),
        peer,
        1200
    }};

    CHECK(&adapter.socket() == &socket);
    CHECK(adapter.peer_endpoint().port() == 4433);
    CHECK(adapter.receive_max_size() == 1200);
    CHECK(socket.is_open());
}

TEST_CASE("UDP session sends flushed QUIC session datagrams over loopback") {
    flowq::context context{};
    udp::socket server_socket{context.io_context(), udp::endpoint{udp::v4(), 0}};
    udp::socket client_socket{context.io_context(), udp::endpoint{udp::v4(), 0}};
    auto server_endpoint = udp::endpoint{::asio::ip::address_v4::loopback(), server_socket.local_endpoint().port()};
    auto client_endpoint = udp::endpoint{::asio::ip::address_v4::loopback(), client_socket.local_endpoint().port()};
    flowq::quic::plaintext_packet_protector protector{};

    flowq::quic::udp_session client{client_socket, flowq::quic::udp_session_config{
        make_config(cid({0x01}), cid({0x02}), flowq::endpoint{"127.0.0.1", server_endpoint.port(), "hq-interop"}, protector),
        server_endpoint,
        1200
    }};
    flowq::quic::udp_session server{server_socket, flowq::quic::udp_session_config{
        make_config(cid({0x02}), cid({0x01}), flowq::endpoint{"127.0.0.1", client_endpoint.port(), "hq-interop"}, protector),
        client_endpoint,
        1200
    }};

    std::optional<flowq::quic::udp_session_send_result> sent;
    std::optional<flowq::quic::udp_session_receive_result> received;
    auto receive_operation = server.async_receive_once().connect(receive_receiver{&received});
    REQUIRE(client.append_stream_data(0, text("hello")).ok());
    REQUIRE(client.queue_stream_data({0}).ok());
    auto send_operation = client.send_pending(at(0ms)).connect(send_receiver{&sent});

    receive_operation.start();
    send_operation.start();
    context.io_context().run();

    REQUIRE(sent.has_value());
    REQUIRE(sent->ok());
    CHECK(sent->bytes_sent > 0);
    REQUIRE(received.has_value());
    REQUIRE(received->ok());
    REQUIRE(received->session_result.stream_deliveries.size() == 1);
    CHECK(received->remote.port() == client_socket.local_endpoint().port());
    CHECK(received->session_result.stream_deliveries[0].stream_id == 0);
    CHECK(as_string(received->session_result.stream_deliveries[0].data) == "hello");
}
