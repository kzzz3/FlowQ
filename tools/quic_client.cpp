// FlowQ QUIC client used by external peer interoperability tests.
#include <flowq/quic/session.hpp>
#include <flowq/quic/openssl_tls_handshake.hpp>
#include <flowq/quic/initial_packet_protector.hpp>
#include <flowq/quic/tls_protector_factory.hpp>
#include <asio.hpp>
#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <span>
#include <thread>
#include <vector>

namespace {

bool send_datagrams(
    asio::ip::udp::socket& socket,
    const std::vector<flowq::quic::outbound_datagram>& datagrams) {
    for (const auto& datagram : datagrams) {
        std::cout << "Sending " << datagram.payload.size() << " bytes" << std::endl;

        auto endpoint = asio::ip::udp::endpoint{asio::ip::make_address(datagram.peer.host), datagram.peer.port};
        socket.send_to(asio::buffer(datagram.payload.data(), datagram.payload.size()), endpoint);
    }
    return true;
}

} // namespace

int main() {
    try {
        using namespace std::chrono_literals;

        asio::io_context io_context;
        asio::ip::udp::socket socket{io_context, asio::ip::udp::endpoint{asio::ip::udp::v4(), 0}};
        socket.non_blocking(true);

        // Create connection IDs
        flowq::quic::connection_id local_cid{flowq::buffer{std::vector<std::byte>{
            static_cast<std::byte>(0x01), static_cast<std::byte>(0x02),
            static_cast<std::byte>(0x03), static_cast<std::byte>(0x04)}}};
        flowq::quic::connection_id remote_cid{flowq::buffer{std::vector<std::byte>{
            static_cast<std::byte>(0x05), static_cast<std::byte>(0x06),
            static_cast<std::byte>(0x07), static_cast<std::byte>(0x08)}}};

        flowq::quic::openssl_tls_config tls_config{.is_client = true};
        tls_config.local_transport_parameters.initial_source_connection_id = flowq::buffer{local_cid.bytes};
        auto tls_adapter = std::make_unique<flowq::quic::openssl_tls_handshake_adapter>(tls_config);

        flowq::quic::crypto_bytes empty_crypto{flowq::quic::tls_encryption_level::initial, 0, flowq::buffer{}};
        auto tls_start = tls_adapter->receive_crypto(empty_crypto);
        if (!tls_start.ok()) {
            std::cerr << "TLS handshake start failed: " << tls_start.message() << std::endl;
            return 1;
        }

        auto initial_tx_protector = flowq::quic::initial_packet_protector::client(remote_cid);
        auto initial_rx_protector = flowq::quic::initial_packet_protector::server(remote_cid);
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
        flowq::quic::session_config session_cfg{};
        session_cfg.role = flowq::quic::connection_role::client;
        session_cfg.local_connection_id = local_cid;
        session_cfg.remote_connection_id = remote_cid;
        session_cfg.peer = flowq::endpoint{"127.0.0.1", 4433, "hq-interop"};
        session_cfg.initial_tx_protector = &initial_tx_protector;
        session_cfg.initial_rx_protector = &initial_rx_protector;
        session_cfg.protection_policy = flowq::quic::packet_protection_policy::production_required;
        session_cfg.tls_adapter = tls_adapter.get();
        session_cfg.pipeline.max_datagram_size = 65535;
        session_cfg.packet_protector_refresh = refresh_tls_protectors;

        flowq::quic::session session{std::move(session_cfg)};

        // Flush to get outbound datagrams
        auto flush_result = session.flush();
        if (!flush_result.ok()) {
            std::cerr << "Flush failed: " << flush_result.error.message() << std::endl;
            return 1;
        }

        std::cout << "Generated " << flush_result.datagrams.size() << " datagram(s)" << std::endl;
        if (flush_result.datagrams.empty()) {
            std::cerr << "No Initial datagram generated" << std::endl;
            return 1;
        }

        send_datagrams(socket, flush_result.datagrams);

        std::array<std::byte, 65535> receive_buffer{};
        auto deadline = std::chrono::steady_clock::now() + 5s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (tls_adapter->state() == flowq::quic::handshake_state::handshake_confirmed) {
                std::cout << "Handshake confirmed" << std::endl;
                return 0;
            }

            asio::ip::udp::endpoint sender;
            asio::error_code receive_error;
            auto received = socket.receive_from(asio::buffer(receive_buffer), sender, 0, receive_error);
            if (receive_error == asio::error::would_block || receive_error == asio::error::try_again) {
                std::this_thread::sleep_for(10ms);
                continue;
            }
            if (receive_error) {
                std::cerr << "Receive failed: " << receive_error.message() << std::endl;
                return 1;
            }

            std::cout << "Received " << received << " bytes" << std::endl;
            auto receive_result = session.on_datagram(flowq::quic::inbound_datagram{
                flowq::buffer{std::span<const std::byte>{receive_buffer.data(), received}},
                flowq::endpoint{sender.address().to_string(), sender.port(), "hq-interop"}
            });
            if (!receive_result.ok()) {
                std::cerr << "Session receive failed: " << receive_result.error.message() << std::endl;
                return 1;
            }
            if (!receive_result.closes.empty()) {
                std::cerr << "Session closed: " << receive_result.closes.front().error.message() << std::endl;
                return 1;
            }
            send_datagrams(socket, receive_result.datagrams);

            auto protector_update = refresh_tls_protectors();
            if (!protector_update.ok()) {
                std::cerr << "TLS protector installation failed: " << protector_update.error.message() << std::endl;
                return 1;
            }
            session.set_packet_protectors(
                protector_update.handshake_tx,
                protector_update.handshake_rx,
                protector_update.application_tx,
                protector_update.application_rx);

            auto next_flush = session.flush();
            if (!next_flush.ok()) {
                std::cerr << "Flush failed: " << next_flush.error.message() << std::endl;
                return 1;
            }
            send_datagrams(socket, next_flush.datagrams);
        }

        std::cerr << "Handshake timed out" << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
