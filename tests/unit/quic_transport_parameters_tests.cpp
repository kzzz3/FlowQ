#include <flowq/quic/session.hpp>
#include <flowq/quic/transport_parameters.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
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

void check_buffer(const flowq::buffer& actual, std::initializer_list<unsigned int> expected) {
    REQUIRE(actual.size() == expected.size());
    std::size_t index = 0;
    for (auto value : expected) {
        CHECK(actual.data()[index] == static_cast<std::byte>(value & 0xffU));
        ++index;
    }
}

} // namespace

TEST_CASE("transport parameters round trip selected QUIC values") {
    flowq::quic::transport_parameters parameters{};
    parameters.max_idle_timeout = 30;
    parameters.max_udp_payload_size = 1350;
    parameters.initial_max_data = 10000;
    parameters.initial_max_stream_data_bidi_local = 2000;
    parameters.initial_max_stream_data_bidi_remote = 3000;
    parameters.initial_max_stream_data_uni = 4000;
    parameters.initial_max_streams_bidi = 16;
    parameters.initial_max_streams_uni = 8;
    parameters.ack_delay_exponent = 12;
    parameters.max_ack_delay = 75;
    parameters.disable_active_migration = true;
    parameters.active_connection_id_limit = 4;
    parameters.original_destination_connection_id = flowq::buffer{bytes({0x05, 0x06, 0x07, 0x08})};
    parameters.initial_source_connection_id = flowq::buffer{bytes({0x01, 0x02, 0x03, 0x04})};
    parameters.retry_source_connection_id = flowq::buffer{bytes({0x09, 0x0a, 0x0b, 0x0c})};

    auto encoded = flowq::quic::encode_transport_parameters(parameters);
    REQUIRE(encoded.ok());
    auto decoded = flowq::quic::decode_transport_parameters(encoded.payload);

    REQUIRE(decoded.ok());
    CHECK(decoded.parameters.max_idle_timeout == 30);
    CHECK(decoded.parameters.max_udp_payload_size == 1350);
    CHECK(decoded.parameters.initial_max_data == 10000);
    CHECK(decoded.parameters.initial_max_stream_data_bidi_local == 2000);
    CHECK(decoded.parameters.initial_max_stream_data_bidi_remote == 3000);
    CHECK(decoded.parameters.initial_max_stream_data_uni == 4000);
    CHECK(decoded.parameters.initial_max_streams_bidi == 16);
    CHECK(decoded.parameters.initial_max_streams_uni == 8);
    CHECK(decoded.parameters.ack_delay_exponent == 12);
    CHECK(decoded.parameters.max_ack_delay == 75);
    CHECK(decoded.parameters.disable_active_migration);
    CHECK(decoded.parameters.active_connection_id_limit == 4);
    REQUIRE(decoded.parameters.original_destination_connection_id.has_value());
    check_buffer(*decoded.parameters.original_destination_connection_id, {0x05, 0x06, 0x07, 0x08});
    REQUIRE(decoded.parameters.initial_source_connection_id.has_value());
    check_buffer(*decoded.parameters.initial_source_connection_id, {0x01, 0x02, 0x03, 0x04});
    REQUIRE(decoded.parameters.retry_source_connection_id.has_value());
    check_buffer(*decoded.parameters.retry_source_connection_id, {0x09, 0x0a, 0x0b, 0x0c});
}

TEST_CASE("transport parameters preserve unknown parameters") {
    flowq::quic::transport_parameters parameters{};
    parameters.initial_max_data = 64;
    parameters.unknown.push_back(flowq::quic::unknown_transport_parameter{0x39, flowq::buffer{bytes({0xaa, 0xbb})}});

    auto encoded = flowq::quic::encode_transport_parameters(parameters);
    REQUIRE(encoded.ok());
    auto decoded = flowq::quic::decode_transport_parameters(encoded.payload);

    REQUIRE(decoded.ok());
    REQUIRE(decoded.parameters.unknown.size() == 1);
    CHECK(decoded.parameters.unknown[0].id == 0x39);
    check_buffer(decoded.parameters.unknown[0].value, {0xaa, 0xbb});
}

TEST_CASE("transport parameters reject duplicate and malformed inputs") {
    CHECK_FALSE(flowq::quic::decode_transport_parameters(flowq::buffer{bytes({0x04, 0x01, 0x01, 0x04, 0x01, 0x02})}).ok());
    CHECK_FALSE(flowq::quic::decode_transport_parameters(flowq::buffer{bytes({0x40})}).ok());
    CHECK_FALSE(flowq::quic::decode_transport_parameters(flowq::buffer{bytes({0x04, 0x40})}).ok());
    CHECK_FALSE(flowq::quic::decode_transport_parameters(flowq::buffer{bytes({0x04, 0x02, 0x40})}).ok());
    CHECK_FALSE(flowq::quic::decode_transport_parameters(flowq::buffer{bytes({0x0c, 0x01, 0x00})}).ok());
    CHECK_FALSE(flowq::quic::decode_transport_parameters(flowq::buffer{bytes({0x0e, 0x01, 0x01})}).ok());
    CHECK_FALSE(flowq::quic::decode_transport_parameters(flowq::buffer{bytes({0x03, 0x02, 0x44, 0xaf})}).ok());
    CHECK_FALSE(flowq::quic::decode_transport_parameters(flowq::buffer{bytes({0x0a, 0x01, 0x15})}).ok());
    CHECK_FALSE(flowq::quic::decode_transport_parameters(flowq::buffer{bytes({0x0b, 0x02, 0x80, 0x00})}).ok());
    CHECK_FALSE(flowq::quic::decode_transport_parameters(flowq::buffer{bytes({
        0x0f, 0x15,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
        0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14})}).ok());
}

TEST_CASE("transport parameters reject invalid encoder inputs") {
    flowq::quic::transport_parameters parameters{};
    parameters.initial_max_data = flowq::quic::max_varint + 1;
    CHECK_FALSE(flowq::quic::encode_transport_parameters(parameters).ok());

    parameters = flowq::quic::transport_parameters{};
    parameters.max_udp_payload_size = 1199;
    CHECK_FALSE(flowq::quic::encode_transport_parameters(parameters).ok());

    parameters = flowq::quic::transport_parameters{};
    parameters.active_connection_id_limit = 1;
    CHECK_FALSE(flowq::quic::encode_transport_parameters(parameters).ok());

    parameters = flowq::quic::transport_parameters{};
    parameters.ack_delay_exponent = 21;
    CHECK_FALSE(flowq::quic::encode_transport_parameters(parameters).ok());

    parameters = flowq::quic::transport_parameters{};
    parameters.max_ack_delay = 16384;
    CHECK_FALSE(flowq::quic::encode_transport_parameters(parameters).ok());

    parameters = flowq::quic::transport_parameters{};
    parameters.unknown.push_back(flowq::quic::unknown_transport_parameter{flowq::quic::max_varint + 1, flowq::buffer{}});
    CHECK_FALSE(flowq::quic::encode_transport_parameters(parameters).ok());

    parameters = flowq::quic::transport_parameters{};
    parameters.unknown.push_back(flowq::quic::unknown_transport_parameter{0x39, flowq::buffer{}});
    parameters.unknown.push_back(flowq::quic::unknown_transport_parameter{0x39, flowq::buffer{}});
    CHECK_FALSE(flowq::quic::encode_transport_parameters(parameters).ok());

    parameters = flowq::quic::transport_parameters{};
    parameters.initial_source_connection_id = flowq::buffer{bytes({
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
        0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14})};
    CHECK_FALSE(flowq::quic::encode_transport_parameters(parameters).ok());
}

TEST_CASE("transport parameters map into connection and session config") {
    flowq::quic::transport_parameters parameters{};
    parameters.max_idle_timeout = 15000;
    parameters.max_udp_payload_size = 1400;
    parameters.initial_max_data = 5000;
    parameters.initial_max_stream_data_bidi_local = 1200;
    parameters.initial_max_stream_data_bidi_remote = 900;
    parameters.initial_max_stream_data_uni = 700;
    parameters.initial_max_streams_bidi = 3;
    parameters.initial_max_streams_uni = 2;
    parameters.ack_delay_exponent = 9;
    parameters.max_ack_delay = 50;
    parameters.disable_active_migration = true;
    parameters.active_connection_id_limit = 6;

    flowq::quic::connection_loop_config connection_config{};
    flowq::quic::apply_transport_parameters(connection_config, parameters);

    CHECK(connection_config.max_idle_timeout == std::chrono::milliseconds{15000});
    CHECK(connection_config.max_udp_payload_size == 1400);
    CHECK(connection_config.max_packet_payload_size == 1400);
    CHECK(connection_config.initial_connection_send_max_data == 5000);
    CHECK(connection_config.initial_stream_send_max_data == 700);
    CHECK(connection_config.initial_max_stream_data_bidi_local == 1200);
    CHECK(connection_config.initial_max_stream_data_bidi_remote == 900);
    CHECK(connection_config.initial_max_stream_data_uni == 700);
    CHECK(connection_config.initial_max_streams_bidi == 3);
    CHECK(connection_config.initial_max_streams_uni == 2);
    CHECK(connection_config.ack_delay_exponent == 9);
    CHECK(connection_config.max_ack_delay == std::chrono::milliseconds{50});
    CHECK(connection_config.disable_active_migration);
    CHECK(connection_config.active_connection_id_limit == 6);

    flowq::quic::session_config session_config{};
    flowq::quic::apply_transport_parameters(session_config, parameters);
    CHECK(session_config.max_idle_timeout == std::chrono::milliseconds{15000});
    CHECK(session_config.max_packet_payload_size == 1400);
    CHECK(session_config.initial_connection_send_max_data == 5000);
    CHECK(session_config.initial_stream_send_max_data == 700);
    CHECK(session_config.initial_max_streams_bidi == 3);
    CHECK(session_config.initial_max_streams_uni == 2);
    CHECK(session_config.ack_delay_exponent == 9);
    CHECK(session_config.max_ack_delay == std::chrono::milliseconds{50});
    CHECK(session_config.disable_active_migration);
}
