#include <flowq/quic/session.hpp>

#include <cstddef>
#include <initializer_list>
#include <utility>
#include <vector>

namespace {

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

flowq::quic::session_config make_config(const flowq::quic::packet_protector& protector) {
    flowq::quic::session_config config{};
    config.role = flowq::quic::connection_role::client;
    config.local_connection_id = cid({0x01});
    config.remote_connection_id = cid({0x02});
    config.peer = flowq::endpoint{"package-consumer.invalid", 4433, "hq-interop"};
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
    flowq::quic::plaintext_packet_protector protector{};
    flowq::quic::session session{make_config(protector)};
    (void)session;
    return 0;
}
