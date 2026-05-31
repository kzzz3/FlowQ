#include <flowq/quic/zero_rtt.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {

class recording_tls_adapter final : public flowq::quic::tls_handshake_adapter {
public:
    flowq::quic::handshake_state state() const noexcept override { return state_; }
    flowq::quic::tls_key_availability key_availability() const noexcept override { return keys_; }
    flowq::error receive_crypto(flowq::quic::crypto_bytes) override { return {}; }
    std::vector<flowq::quic::crypto_bytes> drain_crypto() override { return {}; }
    flowq::error advance() override { return {}; }

    flowq::quic::handshake_state state_{flowq::quic::handshake_state::idle};
    flowq::quic::tls_key_availability keys_{};
};

} // namespace

TEST_CASE("zero_rtt_manager starts unavailable") {
    flowq::quic::zero_rtt_manager manager{};

    CHECK(manager.state() == flowq::quic::zero_rtt_state::unavailable);
}

TEST_CASE("zero_rtt_manager disabled by default") {
    flowq::quic::zero_rtt_manager manager{};
    recording_tls_adapter adapter{};

    auto result = manager.check_availability(adapter);
    CHECK_FALSE(result.can_send());
}

TEST_CASE("zero_rtt_replay_protection accepts new packet") {
    flowq::quic::zero_rtt_replay_protection protection{64};

    CHECK(protection.check_and_record(0));
    CHECK(protection.check_and_record(1));
    CHECK(protection.check_and_record(2));
}

TEST_CASE("zero_rtt_replay_protection rejects duplicate packet") {
    flowq::quic::zero_rtt_replay_protection protection{64};

    CHECK(protection.check_and_record(5));
    CHECK_FALSE(protection.check_and_record(5));
}

TEST_CASE("zero_rtt_replay_protection rejects duplicate after window advances") {
    flowq::quic::zero_rtt_replay_protection protection{64};

    CHECK(protection.check_and_record(5));
    CHECK(protection.check_and_record(6));
    CHECK_FALSE(protection.check_and_record(5));
}

TEST_CASE("zero_rtt_replay_protection rejects packets when window is zero") {
    flowq::quic::zero_rtt_replay_protection protection{0};

    CHECK_FALSE(protection.check_and_record(1));
}

TEST_CASE("zero_rtt_replay_protection accepts packet within window") {
    flowq::quic::zero_rtt_replay_protection protection{64};

    CHECK(protection.check_and_record(100));
    CHECK(protection.check_and_record(50));
    CHECK(protection.check_and_record(101));
}

TEST_CASE("zero_rtt_replay_protection rejects packet outside window") {
    flowq::quic::zero_rtt_replay_protection protection{64};

    CHECK(protection.check_and_record(100));
    CHECK_FALSE(protection.check_and_record(0));  // Too old
}

TEST_CASE("zero_rtt_replay_protection handles large gap") {
    flowq::quic::zero_rtt_replay_protection protection{64};

    CHECK(protection.check_and_record(0));
    CHECK(protection.check_and_record(200));  // Large gap - resets window
    CHECK(protection.check_and_record(201));
}

TEST_CASE("zero_rtt_replay_protection tracks largest seen") {
    flowq::quic::zero_rtt_replay_protection protection{64};

    (void)protection.check_and_record(10);
    (void)protection.check_and_record(20);
    (void)protection.check_and_record(15);

    CHECK(protection.largest_seen() == 20);
}

TEST_CASE("zero_rtt_manager records early data sent") {
    flowq::quic::zero_rtt_manager manager{};

    manager.record_early_data_sent();
    CHECK(manager.state() == flowq::quic::zero_rtt_state::early_data_sent);
}

TEST_CASE("zero_rtt_manager records server decision") {
    flowq::quic::zero_rtt_manager manager{};

    manager.record_server_decision(true);
    CHECK(manager.state() == flowq::quic::zero_rtt_state::accepted);

    manager.record_server_decision(false);
    CHECK(manager.state() == flowq::quic::zero_rtt_state::rejected);
}

TEST_CASE("zero_rtt_manager should retransmit after rejection") {
    flowq::quic::zero_rtt_manager manager{};

    manager.record_server_decision(false);
    CHECK(manager.should_retransmit());
}

TEST_CASE("zero_rtt_manager should not retransmit after acceptance") {
    flowq::quic::zero_rtt_manager manager{};

    manager.record_server_decision(true);
    CHECK_FALSE(manager.should_retransmit());
}
