#include <flowq/quic/packet_pipeline.hpp>
#include "plaintext_packet_protector.hpp"
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
    const flowq::quic::packet_protector& protector) {
    flowq::quic::session_config config{};
    config.role = flowq::quic::connection_role::client;
    config.local_connection_id = std::move(local);
    config.remote_connection_id = std::move(remote);
    config.peer = std::move(peer);
    config.initial_tx_protector = &protector;
    config.initial_rx_protector = &protector;
    config.handshake_tx_protector = &protector;
    config.handshake_rx_protector = &protector;
    config.application_tx_protector = &protector;
    config.application_rx_protector = &protector;
    config.protection_policy = flowq::quic::packet_protection_policy::test_allowed;
    return config;
}

} // namespace

TEST_CASE("example-facing session API compiles and produces an outbound datagram") {
    flowq::quic::test::plaintext_packet_protector protector{};
    flowq::quic::session session{make_config(
        cid({0x01}),
        cid({0x02}),
        flowq::endpoint{"server", 4433, "hq-interop"},
        protector)};

    REQUIRE(session.append_stream_data(0, text("example")).ok());
    REQUIRE(session.queue_stream_data({0}).ok());
    auto sent = session.flush();

    REQUIRE(sent.ok());
    REQUIRE(sent.datagrams.size() == 1);
    CHECK_FALSE(sent.datagrams[0].payload.empty());
}

TEST_CASE("example-facing UDP session configuration compiles") {
    flowq::quic::test::plaintext_packet_protector protector{};
    flowq::quic::udp_session_config config{
        make_config(
            cid({0x01}),
            cid({0x02}),
            flowq::endpoint{"127.0.0.1", 4433, "hq-interop"},
            protector),
        ::asio::ip::udp::endpoint{::asio::ip::address_v4::loopback(), 4433},
        1200
    };

    CHECK(config.peer.port() == 4433);
    CHECK(config.receive_max_size == 1200);
}

TEST_CASE("example-facing protection policy rejects plaintext when production is required") {
    flowq::quic::test::plaintext_packet_protector protector{};
    auto rejected = flowq::quic::assemble_long_packet(flowq::quic::packet_build_request{
        flowq::quic::long_packet_type::initial,
        1,
        cid({0x01}),
        cid({0x02}),
        {},
        flowq::quic::packet_number{flowq::quic::packet_number_space::initial, 0},
        {flowq::quic::frame{flowq::quic::ping_frame{}}},
        &protector,
        flowq::quic::packet_pipeline_config{1200},
        flowq::quic::packet_protection_policy::production_required
    });

    CHECK_FALSE(rejected.ok());
}
