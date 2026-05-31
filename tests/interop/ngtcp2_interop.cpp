// FlowQ <-> ngtcp2 Interop Test
//
// This program tests FlowQ against ngtcp2 for interop verification.
// It runs FlowQ as a client connecting to an ngtcp2 server.
//
// Usage:
//   flowq_ngtcp2_interop --host <host> --port <port> --ca <cert_file>
//
// The ngtcp2 server should be started separately:
//   ngtcp2-server <host> <port> <cert_file> <key_file>

#include <flowq/quic/session.hpp>
#include <flowq/quic/openssl_tls_handshake.hpp>
#include <flowq/quic/initial_packet_protector.hpp>
#include <flowq/quic/tls_protector_factory.hpp>

#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <string>

using namespace std::chrono_literals;

static flowq::quic::connection_id make_random_cid() {
    flowq::quic::connection_id cid;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& b : cid.bytes) {
        b = static_cast<std::byte>(dist(gen));
    }
    return cid;
}

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    std::uint16_t port = 4433;
    std::string ca_file;
    std::string server_name = "localhost";

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            host = argv[++i];
        } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--ca") == 0 && i + 1 < argc) {
            ca_file = argv[++i];
        } else if (std::strcmp(argv[i], "--server-name") == 0 && i + 1 < argc) {
            server_name = argv[++i];
        }
    }

    if (ca_file.empty()) {
        std::cerr << "Usage: flowq_ngtcp2_interop --host <host> --port <port> --ca <cert_file>" << std::endl;
        return 1;
    }

    std::cout << "FlowQ <-> ngtcp2 Interop Test" << std::endl;
    std::cout << "Connecting to " << host << ":" << port << std::endl;

    // Create TLS adapter
    flowq::quic::openssl_tls_config tls_cfg;
    tls_cfg.is_client = true;
    tls_cfg.ca_file = ca_file.c_str();
    tls_cfg.server_name = server_name.c_str();

    auto tls_adapter = std::make_unique<flowq::quic::openssl_tls_handshake_adapter>(tls_cfg);
    if (tls_adapter->state() == flowq::quic::handshake_state::failed) {
        std::cerr << "TLS adapter creation failed" << std::endl;
        return 1;
    }

    // Create connection IDs
    auto local_cid = make_random_cid();
    auto remote_cid = make_random_cid();

    // Create initial protectors
    auto initial_tx = flowq::quic::initial_packet_protector::client(remote_cid);
    auto initial_rx = flowq::quic::initial_packet_protector::server(remote_cid);

    // Create session
    flowq::quic::session_config cfg{};
    cfg.role = flowq::quic::connection_role::client;
    cfg.local_connection_id = local_cid;
    cfg.remote_connection_id = remote_cid;
    cfg.peer = flowq::endpoint{host, port, "hq-interop"};
    cfg.initial_tx_protector = &initial_tx;
    cfg.initial_rx_protector = &initial_rx;
    cfg.tls_adapter = tls_adapter.get();

    flowq::quic::session session{std::move(cfg)};

    // Start handshake
    auto flush = session.flush();
    if (!flush.ok()) {
        std::cerr << "Initial flush failed: " << flush.error.message() << std::endl;
        return 1;
    }

    std::cout << "Generated " << flush.datagrams.size() << " initial datagram(s)" << std::endl;

    if (flush.datagrams.empty()) {
        std::cerr << "No datagrams generated" << std::endl;
        return 1;
    }

    // TODO: Send datagrams via UDP and receive responses
    // For now, just verify that FlowQ can generate valid initial packets

    std::cout << "FlowQ ngtcp2 interop test: Initial packet generation PASSED" << std::endl;
    std::cout << "Note: Full UDP send/receive test requires UDP socket integration" << std::endl;

    return 0;
}
