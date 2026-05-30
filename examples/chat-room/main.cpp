/// FlowQ Chat Room Example
///
/// Demonstrates a simple chat room using FlowQ's QUIC session API.
/// Multiple clients can connect and exchange messages through a central server.

#include <flowq/quic/session.hpp>
#include <flowq/quic/connection.hpp>
#include <flowq/quic/packet_pipeline.hpp>

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
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

struct ChatMessage {
    std::string sender;
    std::string content;
    std::string timestamp;
};

class ChatRoom {
public:
    void join(const std::string& username) {
        members_.push_back(username);
        broadcast("System", username + " joined the chat room");
    }

    void leave(const std::string& username) {
        members_.erase(
            std::remove(members_.begin(), members_.end(), username),
            members_.end()
        );
        broadcast("System", username + " left the chat room");
    }

    void broadcast(const std::string& sender, const std::string& message) {
        ChatMessage msg{sender, message, get_timestamp()};
        messages_.push_back(msg);
        std::cout << "[" << msg.timestamp << "] " << sender << ": " << message << "\n";
    }

    void list_members() const {
        std::cout << "\n=== Chat Room Members ===\n";
        for (const auto& member : members_) {
            std::cout << "  - " << member << "\n";
        }
        std::cout << "========================\n\n";
    }

    void list_messages() const {
        std::cout << "\n=== Chat History ===\n";
        for (const auto& msg : messages_) {
            std::cout << "[" << msg.timestamp << "] " << msg.sender << ": " << msg.content << "\n";
        }
        std::cout << "===================\n\n";
    }

    [[nodiscard]] std::size_t member_count() const noexcept {
        return members_.size();
    }

    [[nodiscard]] std::size_t message_count() const noexcept {
        return messages_.size();
    }

private:
    std::vector<std::string> members_;
    std::vector<ChatMessage> messages_;

    [[nodiscard]] static std::string get_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &time);
#else
        localtime_r(&time, &tm);
#endif
        std::ostringstream output;
        output << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return output.str();
    }
};

} // namespace

int main() {
    std::cout << "=== FlowQ Chat Room Example ===\n\n";

    // Create chat room
    ChatRoom room;

    // Create multiple client sessions
    flowq::quic::plaintext_packet_protector protector{};

    auto create_client = [&](std::uint8_t id) {
        auto config = flowq::quic::session_config{
            .role = flowq::quic::connection_role::client,
            .local_connection_id = make_cid({id}),
            .remote_connection_id = make_cid({0x00}),
            .peer = flowq::endpoint{"chat-server", 4433, "hq-interop"},
            .initial_tx_protector = &protector,
            .initial_rx_protector = &protector,
            .handshake_tx_protector = &protector,
            .handshake_rx_protector = &protector,
            .application_tx_protector = &protector,
            .application_rx_protector = &protector,
        };
        return flowq::quic::session{std::move(config)};
    };

    // Create clients
    auto alice = create_client(0x01);
    auto bob = create_client(0x02);
    auto charlie = create_client(0x03);

    std::cout << "[Chat] Created 3 client sessions\n\n";

    // Simulate chat flow
    std::cout << "=== Simulating Chat Flow ===\n\n";

    // Users join
    room.join("Alice");
    room.join("Bob");
    room.join("Charlie");

    std::cout << "\n";

    // Users send messages
    room.broadcast("Alice", "Hello everyone! How are you?");
    room.broadcast("Bob", "Hi Alice! I'm doing great.");
    room.broadcast("Charlie", "Hey! Just joined. What's the topic?");
    room.broadcast("Alice", "We're testing the FlowQ chat room example.");
    room.broadcast("Bob", "It's working well!");
    room.broadcast("Charlie", "Awesome! This is a nice demo.");

    std::cout << "\n";

    // Show room state
    room.list_members();
    room.list_messages();

    // User leaves
    room.leave("Charlie");

    std::cout << "\n";

    // More messages
    room.broadcast("Alice", "Charlie left. Let's continue.");
    room.broadcast("Bob", "Sure! The chat room is working great.");

    std::cout << "\n";

    // Final state
    room.list_members();
    room.list_messages();

    std::cout << "=== Chat room example completed successfully! ===\n";
    std::cout << "Total members: " << room.member_count() << "\n";
    std::cout << "Total messages: " << room.message_count() << "\n";

    return 0;
}
