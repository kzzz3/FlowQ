#include <flowq/quic/session.hpp>

#include <chrono>
#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using clock_type = std::chrono::steady_clock;

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
    flowq::quic::connection_role role,
    flowq::quic::connection_id local,
    flowq::quic::connection_id remote,
    flowq::endpoint peer,
    const flowq::quic::packet_protector& protector) {
    flowq::quic::session_config config{};
    config.role = role;
    config.local_connection_id = std::move(local);
    config.remote_connection_id = std::move(remote);
    config.peer = std::move(peer);
    config.initial_tx_protector = &protector;
    config.initial_rx_protector = &protector;
    config.handshake_tx_protector = &protector;
    config.handshake_rx_protector = &protector;
    config.application_tx_protector = &protector;
    config.application_rx_protector = &protector;
    return config;
}

} // namespace

int main() {
    // Plaintext packet protection is accepted only when test policy is enabled.
    flowq::quic::plaintext_packet_protector protector{};
    flowq::quic::session client{make_config(
        flowq::quic::connection_role::client,
        cid({0x01}),
        cid({0x02}),
        flowq::endpoint{"server", 4433, "hq-interop"},
        protector)};
    flowq::quic::session server{make_config(
        flowq::quic::connection_role::server,
        cid({0x02}),
        cid({0x01}),
        flowq::endpoint{"client", 1111, "hq-interop"},
        protector)};

    if (!client.append_stream_data(0, text("hello from FlowQ")).ok()) {
        std::cerr << "failed to append stream data\n";
        return 1;
    }
    if (!client.queue_stream_data({0}).ok()) {
        std::cerr << "failed to queue stream data\n";
        return 1;
    }

    auto sent = client.flush(at(0ms));
    if (!sent.ok() || sent.datagrams.size() != 1) {
        std::cerr << "expected one outbound datagram\n";
        return 1;
    }

    auto received = server.on_datagram(flowq::quic::inbound_datagram{std::move(sent.datagrams[0].payload), sent.datagrams[0].peer});
    if (!received.ok() || received.stream_deliveries.size() != 1 || !received.stream_deliveries[0].ok()) {
        std::cerr << "expected one stream delivery\n";
        return 1;
    }

    const auto delivered = as_string(received.stream_deliveries[0].data);
    if (delivered != "hello from FlowQ") {
        std::cerr << "unexpected delivered text: " << delivered << '\n';
        return 1;
    }

    std::cout << "FlowQ in-memory loopback delivered: " << delivered << '\n';
    return 0;
}
