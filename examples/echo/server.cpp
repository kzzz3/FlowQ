/// FlowQ Echo Server Example
///
/// Demonstrates a simple echo server using FlowQ's QUIC-like session.
/// The server receives messages and echoes them back to the client.
///
/// Note: This is a deterministic in-memory example using FlowQ's
/// non-production QUIC-like protocol primitives.

#include <flowq/quic/session.hpp>
#include <flowq/quic/connection.hpp>
#include <flowq/quic/packet_pipeline.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace {

flowq::quic::connection_id make_cid(std::initializer_list<unsigned char> bytes) {
    std::vector<std::byte> data;
    for (auto b : bytes) {
        data.push_back(static_cast<std::byte>(b));
    }
    return flowq::quic::connection_id{flowq::buffer{data}};
}

flowq::buffer make_text(const std::string& text) {
    std::vector<std::byte> bytes;
    for (char c : text) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
    return flowq::buffer{bytes};
}

std::string to_string(const flowq::buffer& buf) {
    std::string result;
    for (std::size_t i = 0; i < buf.size(); ++i) {
        result.push_back(static_cast<char>(buf.data()[i]));
    }
    return result;
}

} // namespace

int main() {
    std::cout << "=== FlowQ Echo Server Example ===\n\n";

    // Create server and client sessions
    flowq::quic::plaintext_packet_protector protector{};

    auto server_config = flowq::quic::session_config{
        .role = flowq::quic::connection_role::server,
        .local_connection_id = make_cid({0x02}),
        .remote_connection_id = make_cid({0x01}),
        .peer = flowq::endpoint{"client", 1111, "hq-interop"},
        .initial_protector = &protector,
        .handshake_protector = &protector,
        .application_protector = &protector,
    };

    auto client_config = flowq::quic::session_config{
        .role = flowq::quic::connection_role::client,
        .local_connection_id = make_cid({0x01}),
        .remote_connection_id = make_cid({0x02}),
        .peer = flowq::endpoint{"server", 4433, "hq-interop"},
        .initial_protector = &protector,
        .handshake_protector = &protector,
        .application_protector = &protector,
    };

    flowq::quic::session server{std::move(server_config)};
    flowq::quic::session client{std::move(client_config)};

    // Client sends a message
    std::string message = "Hello, FlowQ Echo Server!";
    std::cout << "[Client] Sending: " << message << "\n";

    auto send_result = client.queue_stream_data({0}, 1, 16);
    if (!send_result.ok()) {
        std::cerr << "Failed to queue stream data\n";
        return 1;
    }

    // Client flushes to get outbound datagrams
    auto client_flush = client.flush();
    if (!client_flush.ok()) {
        std::cerr << "Client flush failed\n";
        return 1;
    }

    std::cout << "[Client] Generated " << client_flush.datagrams.size() << " datagram(s)\n";

    // Server receives the datagram
    for (auto& datagram : client_flush.datagrams) {
        std::cout << "[Server] Received datagram from " << datagram.peer.host << ":" << datagram.peer.port << "\n";

        auto receive_result = server.on_datagram(flowq::quic::inbound_datagram{std::move(datagram.payload), datagram.peer});
        if (!receive_result.ok()) {
            std::cerr << "Server receive failed\n";
            return 1;
        }

        // Process received stream data
        for (auto& delivery : receive_result.stream_deliveries) {
            std::string received_text = to_string(delivery.data);
            std::cout << "[Server] Echo: " << received_text << "\n";
        }
    }

    // Server sends acknowledgment
    auto server_flush = server.flush();
    if (!server_flush.ok()) {
        std::cerr << "Server flush failed\n";
        return 1;
    }

    std::cout << "[Server] Generated " << server_flush.datagrams.size() << " response datagram(s)\n";

    // Client receives acknowledgment
    for (auto& datagram : server_flush.datagrams) {
        auto ack_result = client.on_datagram(flowq::quic::inbound_datagram{std::move(datagram.payload), datagram.peer});
        if (!ack_result.ok()) {
            std::cerr << "Client receive failed\n";
            return 1;
        }
        std::cout << "[Client] Received acknowledgment\n";
    }

    std::cout << "\n=== Echo example completed successfully! ===\n";
    return 0;
}
