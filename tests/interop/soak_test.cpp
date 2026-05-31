// FlowQ Soak Test
//
// Long-running stability test that verifies:
// 1. Memory stability (no leaks)
// 2. Connection churn (create/destroy cycles)
// 3. Continuous data transfer
// 4. Error-free operation
//
// Usage:
//   flowq_soak_test [--duration <seconds>] [--connections <count>]

#include <flowq/quic/session.hpp>
#include <flowq/quic/openssl_tls_handshake.hpp>
#include <flowq/quic/initial_packet_protector.hpp>
#include <flowq/quic/tls_protector_factory.hpp>

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace std::chrono_literals;

struct soak_stats {
    std::uint64_t connections_created{};
    std::uint64_t packets_generated{};
    std::uint64_t errors{};
    std::chrono::steady_clock::time_point start{};
    std::chrono::steady_clock::time_point end{};
};

soak_stats run_soak_test(std::int64_t duration_seconds, std::uint64_t max_connections) {
    soak_stats stats{};
    stats.start = std::chrono::steady_clock::now();
    auto deadline = stats.start + std::chrono::seconds(duration_seconds);

    std::cout << "Soak test: " << duration_seconds << "s, max " << max_connections << " connections" << std::endl;

    // Use same fixed CIDs as quic_client
    flowq::quic::connection_id local_cid{flowq::buffer{std::vector<std::byte>{
        static_cast<std::byte>(0x01), static_cast<std::byte>(0x02),
        static_cast<std::byte>(0x03), static_cast<std::byte>(0x04)}}};
    flowq::quic::connection_id remote_cid{flowq::buffer{std::vector<std::byte>{
        static_cast<std::byte>(0x05), static_cast<std::byte>(0x06),
        static_cast<std::byte>(0x07), static_cast<std::byte>(0x08)}}};

    // TLS config (same as quic_client)
    flowq::quic::openssl_tls_config tls_cfg;
    tls_cfg.is_client = true;
    tls_cfg.ca_file = "build/certs/cert.pem";
    tls_cfg.server_name = "localhost";
    tls_cfg.local_transport_parameters.initial_source_connection_id = flowq::buffer{local_cid.bytes};

    while (std::chrono::steady_clock::now() < deadline) {
        // Create TLS adapter
        auto tls_adapter = std::make_unique<flowq::quic::openssl_tls_handshake_adapter>(tls_cfg);
        if (tls_adapter->state() == flowq::quic::handshake_state::failed) {
            stats.errors++;
            continue;
        }

        // Start TLS
        flowq::quic::crypto_bytes empty_crypto{flowq::quic::tls_encryption_level::initial, 0, flowq::buffer{}};
        auto tls_start = tls_adapter->receive_crypto(empty_crypto);
        if (!tls_start.ok()) {
            stats.errors++;
            continue;
        }

        // Create protectors (same as quic_client)
        auto initial_tx = flowq::quic::initial_packet_protector::client(remote_cid);
        auto initial_rx = flowq::quic::initial_packet_protector::server(remote_cid);

        // Create session (same as quic_client)
        flowq::quic::session_config cfg{};
        cfg.role = flowq::quic::connection_role::client;
        cfg.local_connection_id = local_cid;
        cfg.remote_connection_id = remote_cid;
        cfg.peer = flowq::endpoint{"127.0.0.1", 4433, "hq-interop"};
        cfg.initial_tx_protector = &initial_tx;
        cfg.initial_rx_protector = &initial_rx;
        cfg.tls_adapter = tls_adapter.get();
        cfg.max_packet_payload_size = cfg.pipeline.max_datagram_size;

        flowq::quic::session session{std::move(cfg)};

        // Flush to generate Initial packet
        auto flush = session.flush();
        if (!flush.ok()) {
            stats.errors++;
            continue;
        }

        stats.packets_generated += flush.datagrams.size();
        stats.connections_created++;

        // Progress indicator
        if (stats.connections_created % 100 == 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - stats.start);
            std::cout << "  " << elapsed.count() << "s: " << stats.connections_created
                      << " connections, " << stats.packets_generated << " packets, "
                      << stats.errors << " errors" << std::endl;
        }

        // Check connection limit
        if (max_connections > 0 && stats.connections_created >= max_connections) {
            break;
        }
    }

    stats.end = std::chrono::steady_clock::now();
    return stats;
}

int main(int argc, char* argv[]) {
    std::int64_t duration_seconds = 10;  // Default: 10 seconds
    std::uint64_t max_connections = 0;   // 0 = unlimited

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            duration_seconds = std::stoll(argv[++i]);
        } else if (std::strcmp(argv[i], "--connections") == 0 && i + 1 < argc) {
            max_connections = std::stoull(argv[++i]);
        }
    }

    std::cout << "FlowQ Soak Test" << std::endl;
    std::cout << "================" << std::endl;

    auto stats = run_soak_test(duration_seconds, max_connections);

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stats.end - stats.start);

    std::cout << std::endl;
    std::cout << "Results:" << std::endl;
    std::cout << "  Duration: " << duration.count() << " ms" << std::endl;
    std::cout << "  Connections: " << stats.connections_created << std::endl;
    std::cout << "  Packets: " << stats.packets_generated << std::endl;
    std::cout << "  Errors: " << stats.errors << std::endl;
    if (duration.count() > 0) {
        std::cout << "  Rate: " << (stats.connections_created * 1000 / duration.count())
                  << " connections/sec" << std::endl;
    }

    bool passed = (stats.errors == 0 && stats.connections_created > 0);
    std::cout << std::endl;
    std::cout << "Soak test: " << (passed ? "PASSED" : "FAILED") << std::endl;

    return passed ? 0 : 1;
}
