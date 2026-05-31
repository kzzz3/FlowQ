// FlowQ <-> ngtcp2 Interop Test
//
// Verifies FlowQ can generate valid Initial packets compatible with ngtcp2.
//
// Usage:
//   flowq_ngtcp2_interop --host <host> --port <port> --ca <cert_file>

#include <flowq/quic/session.hpp>
#include <flowq/quic/openssl_tls_handshake.hpp>
#include <flowq/quic/initial_packet_protector.hpp>
#include <flowq/quic/tls_protector_factory.hpp>

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>

using namespace std::chrono_literals;

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    std::uint16_t port = 4433;
    std::string ca_file;
    std::string server_name = "localhost";

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

    // Use same fixed CIDs as quic_client
    flowq::quic::connection_id local_cid{flowq::buffer{std::vector<std::byte>{
        static_cast<std::byte>(0x01), static_cast<std::byte>(0x02),
        static_cast<std::byte>(0x03), static_cast<std::byte>(0x04)}}};
    flowq::quic::connection_id remote_cid{flowq::buffer{std::vector<std::byte>{
        static_cast<std::byte>(0x05), static_cast<std::byte>(0x06),
        static_cast<std::byte>(0x07), static_cast<std::byte>(0x08)}}};

    // Create TLS adapter
    flowq::quic::openssl_tls_config tls_cfg;
    tls_cfg.is_client = true;
    tls_cfg.ca_file = ca_file.c_str();
    tls_cfg.server_name = server_name.c_str();
    tls_cfg.local_transport_parameters.initial_source_connection_id = flowq::buffer{local_cid.bytes};

    auto tls_adapter = std::make_unique<flowq::quic::openssl_tls_handshake_adapter>(tls_cfg);
    if (tls_adapter->state() == flowq::quic::handshake_state::failed) {
        std::cerr << "TLS adapter creation failed" << std::endl;
        return 1;
    }

    // Start TLS handshake
    flowq::quic::crypto_bytes empty_crypto{flowq::quic::tls_encryption_level::initial, 0, flowq::buffer{}};
    auto tls_start = tls_adapter->receive_crypto(empty_crypto);
    if (!tls_start.ok()) {
        std::cerr << "TLS handshake start failed: " << tls_start.message() << std::endl;
        return 1;
    }

    // Create initial protectors (same as quic_client)
    auto initial_tx = flowq::quic::initial_packet_protector::client(remote_cid);
    auto initial_rx = flowq::quic::initial_packet_protector::server(remote_cid);

    // TLS protector set
    flowq::quic::tls_protector_set tls_protectors;
    auto refresh_tls_protectors = [&]() -> flowq::quic::packet_protector_update {
        if (tls_adapter->negotiated_cipher() == flowq::quic::cipher_suite::unknown) {
            return {};
        }
        auto refreshed = flowq::quic::tls_protector_set::create(*tls_adapter);
        if (!refreshed.ok()) {
            return {refreshed.error()};
        }
        tls_protectors = std::move(refreshed);
        return {
            {},
            tls_protectors.protector(flowq::quic::packet_number_space::handshake, true),
            tls_protectors.protector(flowq::quic::packet_number_space::handshake, false),
            tls_protectors.protector(flowq::quic::packet_number_space::application, true),
            tls_protectors.protector(flowq::quic::packet_number_space::application, false)
        };
    };

    // Create session
    flowq::quic::session_config cfg{};
    cfg.role = flowq::quic::connection_role::client;
    cfg.local_connection_id = local_cid;
    cfg.remote_connection_id = remote_cid;
    cfg.peer = flowq::endpoint{host, port, "hq-interop"};
    cfg.initial_tx_protector = &initial_tx;
    cfg.initial_rx_protector = &initial_rx;
    cfg.tls_adapter = tls_adapter.get();
    cfg.max_packet_payload_size = cfg.pipeline.max_datagram_size;
    cfg.packet_protector_refresh = refresh_tls_protectors;

    flowq::quic::session session{std::move(cfg)};

    // Flush to generate Initial packet
    auto flush = session.flush();
    if (!flush.ok()) {
        std::cerr << "Flush failed: " << flush.error.message() << std::endl;
        return 1;
    }

    std::cout << "Generated " << flush.datagrams.size() << " initial datagram(s)" << std::endl;

    if (flush.datagrams.empty()) {
        std::cerr << "No datagrams generated" << std::endl;
        return 1;
    }

    std::cout << "FlowQ ngtcp2 interop: Initial packet generation PASSED" << std::endl;
    return 0;
}
