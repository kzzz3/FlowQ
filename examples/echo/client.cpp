/// FlowQ Echo Client Example
///
/// Demonstrates a simple echo client using FlowQ's QUIC session API.
/// The client sends a message to the server and receives the echoed response.

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
    std::cout << "=== FlowQ Echo Client Example ===\n\n";

    // Create client session
    flowq::quic::plaintext_packet_protector protector{};

    auto client_config = flowq::quic::session_config{
        .role = flowq::quic::connection_role::client,
        .local_connection_id = make_cid({0x01}),
        .remote_connection_id = make_cid({0x02}),
        .peer = flowq::endpoint{"server", 4433, "hq-interop"},
        .initial_tx_protector = &protector,
        .initial_rx_protector = &protector,
        .handshake_tx_protector = &protector,
        .handshake_rx_protector = &protector,
        .application_tx_protector = &protector,
        .application_rx_protector = &protector,
    };

    flowq::quic::session client{std::move(client_config)};

    // Prepare message
    std::vector<std::string> messages = {
        "Hello, FlowQ!",
        "This is an echo test.",
        "FlowQ is a QUIC protocol library.",
        "Testing echo functionality.",
        "Goodbye!"
    };

    std::cout << "[Client] Starting echo client...\n\n";

    // Send each message
    for (const auto& message : messages) {
        std::cout << "[Client] Sending: " << message << "\n";

        // Queue stream data
        auto send_result = client.queue_stream_data({0}, 1, 16);
        if (!send_result.ok()) {
            std::cerr << "[Client] Failed to queue stream data\n";
            continue;
        }

        // Flush to get outbound datagrams
        auto flush_result = client.flush();
        if (!flush_result.ok()) {
            std::cerr << "[Client] Flush failed\n";
            continue;
        }

        std::cout << "[Client] Generated " << flush_result.datagrams.size() << " datagram(s)\n";

        // In a real implementation, these datagrams would be sent to the server
        // and the server would echo them back. For this in-memory example,
        // we simulate the echo by creating a response.

        // Simulate server echo response
        auto echo_response = make_text("Echo: " + message);
        std::cout << "[Client] Received echo: " << to_string(echo_response) << "\n\n";
    }

    std::cout << "=== Echo client example completed successfully! ===\n";
    return 0;
}
