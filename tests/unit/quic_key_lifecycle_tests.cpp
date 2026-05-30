#include <flowq/quic/key_lifecycle.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("key lifecycle records send and receive availability events") {
    flowq::quic::key_lifecycle_state lifecycle{};

    CHECK_FALSE(lifecycle.available(flowq::quic::encryption_level::initial, flowq::quic::key_direction::send));
    CHECK_FALSE(lifecycle.available(flowq::quic::encryption_level::one_rtt, flowq::quic::key_direction::receive));

    lifecycle.install({flowq::quic::encryption_level::initial, flowq::quic::key_direction::send});
    lifecycle.install({flowq::quic::encryption_level::initial, flowq::quic::key_direction::receive});
    lifecycle.install({flowq::quic::encryption_level::handshake, flowq::quic::key_direction::send});
    lifecycle.install({flowq::quic::encryption_level::handshake, flowq::quic::key_direction::receive});
    lifecycle.install({flowq::quic::encryption_level::zero_rtt, flowq::quic::key_direction::send});
    lifecycle.install({flowq::quic::encryption_level::one_rtt, flowq::quic::key_direction::receive});

    CHECK(lifecycle.available(flowq::quic::encryption_level::initial, flowq::quic::key_direction::send));
    CHECK(lifecycle.available(flowq::quic::encryption_level::initial, flowq::quic::key_direction::receive));
    CHECK(lifecycle.available(flowq::quic::encryption_level::handshake, flowq::quic::key_direction::send));
    CHECK(lifecycle.available(flowq::quic::encryption_level::handshake, flowq::quic::key_direction::receive));
    CHECK(lifecycle.available(flowq::quic::encryption_level::zero_rtt, flowq::quic::key_direction::send));
    CHECK(lifecycle.available(flowq::quic::encryption_level::one_rtt, flowq::quic::key_direction::receive));
}

TEST_CASE("key lifecycle derives discard decisions from TLS state") {
    flowq::quic::key_lifecycle_state lifecycle{};

    lifecycle.observe_tls(flowq::quic::handshake_state::handshaking, flowq::quic::tls_key_availability{true, false, false});
    CHECK(lifecycle.available(flowq::quic::encryption_level::initial, flowq::quic::key_direction::send));
    CHECK(lifecycle.available(flowq::quic::encryption_level::initial, flowq::quic::key_direction::receive));
    CHECK_FALSE(lifecycle.discarded(flowq::quic::packet_number_space::initial));

    lifecycle.observe_tls(flowq::quic::handshake_state::handshaking, flowq::quic::tls_key_availability{true, true, false});
    CHECK(lifecycle.available(flowq::quic::encryption_level::handshake, flowq::quic::key_direction::send));
    CHECK(lifecycle.available(flowq::quic::encryption_level::handshake, flowq::quic::key_direction::receive));
    CHECK(lifecycle.discarded(flowq::quic::packet_number_space::initial));
    CHECK_FALSE(lifecycle.discarded(flowq::quic::packet_number_space::handshake));

    lifecycle.observe_tls(flowq::quic::handshake_state::handshake_confirmed, flowq::quic::tls_key_availability{false, true, true});
    CHECK(lifecycle.available(flowq::quic::encryption_level::one_rtt, flowq::quic::key_direction::send));
    CHECK(lifecycle.available(flowq::quic::encryption_level::one_rtt, flowq::quic::key_direction::receive));
    CHECK_FALSE(lifecycle.discarded(flowq::quic::packet_number_space::handshake));

    lifecycle.discard(flowq::quic::packet_number_space::handshake);
    CHECK(lifecycle.discarded(flowq::quic::packet_number_space::handshake));
}

TEST_CASE("key lifecycle discard operations are idempotent") {
    flowq::quic::key_lifecycle_state lifecycle{};

    CHECK_FALSE(lifecycle.discarded(flowq::quic::packet_number_space::initial));
    lifecycle.discard(flowq::quic::packet_number_space::initial);
    lifecycle.discard(flowq::quic::packet_number_space::initial);
    CHECK(lifecycle.discarded(flowq::quic::packet_number_space::initial));
    CHECK_FALSE(lifecycle.discarded(flowq::quic::packet_number_space::application));
}
