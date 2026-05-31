#include "production_quic_test_context.hpp"
#include <flowq/quic/session.hpp>
#include <flowq/quic/udp_session.hpp>

#include <catch2/catch_test_macros.hpp>

#include <asio/ip/address_v4.hpp>
#include <asio/ip/udp.hpp>

#include <cstddef>
#include <initializer_list>
#include <string>
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

flowq::buffer text(std::string value) {
    std::vector<std::byte> output;
    output.reserve(value.size());
    for (auto character : value) {
        output.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return flowq::buffer{output};
}

flowq::quic::session_config make_config(
    flowq::quic::connection_id local,
    flowq::quic::connection_id remote,
    flowq::endpoint peer,
    flowq::quic::test::production_packet_protectors& protectors,
    flowq::quic::test::confirmed_tls_adapter& tls) {
    flowq::quic::session_config config{};
    config.role = flowq::quic::connection_role::client;
    config.local_connection_id = std::move(local);
    config.remote_connection_id = std::move(remote);
    config.peer = std::move(peer);
    config.tls_adapter = &tls;
    flowq::quic::test::mark_application_ready(config.key_lifecycle);
    config.initial_tx_protector = &protectors.client_initial_tx;
    config.initial_rx_protector = &protectors.server_initial_tx;
    config.handshake_tx_protector = &protectors.client_handshake_tx;
    config.handshake_rx_protector = &protectors.server_handshake_tx;
    config.application_tx_protector = &protectors.client_application_tx;
    config.application_rx_protector = &protectors.server_application_tx;
    return config;
}

} // namespace

TEST_CASE("example-facing session API compiles and produces an outbound datagram") {
    auto protectors = flowq::quic::test::make_production_packet_protectors();
    REQUIRE(protectors.ok());
    flowq::quic::test::confirmed_tls_adapter tls{};
    flowq::quic::session session{make_config(
        cid({0x01}),
        cid({0x02}),
        flowq::endpoint{"server", 4433, "hq-interop"},
        protectors,
        tls)};

    REQUIRE(session.append_stream_data(0, text("example")).ok());
    REQUIRE(session.queue_stream_data({0}).ok());
    auto sent = session.flush();

    REQUIRE(sent.ok());
    REQUIRE(sent.datagrams.size() == 1);
    CHECK_FALSE(sent.datagrams[0].payload.empty());
}

TEST_CASE("example-facing UDP session configuration compiles") {
    auto protectors = flowq::quic::test::make_production_packet_protectors();
    REQUIRE(protectors.ok());
    flowq::quic::test::confirmed_tls_adapter tls{};
    flowq::quic::udp_session_config config{
        make_config(
            cid({0x01}),
            cid({0x02}),
            flowq::endpoint{"127.0.0.1", 4433, "hq-interop"},
            protectors,
            tls),
        ::asio::ip::udp::endpoint{::asio::ip::address_v4::loopback(), 4433},
        1200
    };

    CHECK(config.peer.port() == 4433);
    CHECK(config.receive_max_size == 1200);
}

TEST_CASE("example-facing session API keeps production packet protection configured") {
    auto protectors = flowq::quic::test::make_production_packet_protectors();
    REQUIRE(protectors.ok());
    flowq::quic::test::confirmed_tls_adapter tls{};
    auto config = make_config(
        cid({0x01}),
        cid({0x02}),
        flowq::endpoint{"server", 4433, "hq-interop"},
        protectors,
        tls);

    CHECK(config.application_tx_protector->provider_status().packet_protection_ready());
    CHECK(config.tls_adapter->provider_status().key_schedule_ready());
    CHECK(config.key_lifecycle.available(flowq::quic::encryption_level::one_rtt, flowq::quic::key_direction::send));
}
