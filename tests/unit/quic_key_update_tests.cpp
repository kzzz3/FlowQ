#include <catch2/catch_test_macros.hpp>
#include <flowq/quic/key_update.hpp>

using namespace flowq::quic;

TEST_CASE("key_update_state initial state") {
    key_update_state state;
    CHECK(state.current_phase() == key_phase::phase_0);
    CHECK_FALSE(state.update_in_progress());
    CHECK_FALSE(state.can_update());  // Haven't received any packets yet
}

TEST_CASE("key_update_state can update after receiving packet") {
    key_update_state state;
    state.receive_key_phase(key_phase::phase_0);
    CHECK(state.can_update());
}

TEST_CASE("key_update_state initiate update toggles phase") {
    key_update_state state;
    state.receive_key_phase(key_phase::phase_0);
    
    auto new_phase = state.initiate_update();
    CHECK(new_phase == key_phase::phase_1);
    CHECK(state.current_phase() == key_phase::phase_1);
    CHECK(state.update_in_progress());
    CHECK_FALSE(state.can_update());  // Update in progress
}

TEST_CASE("key_update_state complete update allows new update") {
    key_update_state state;
    state.receive_key_phase(key_phase::phase_0);
    
    state.initiate_update();
    state.complete_update();
    
    CHECK_FALSE(state.update_in_progress());
    // Need to receive a packet with current phase before updating again
    state.receive_key_phase(key_phase::phase_1);
    CHECK(state.can_update());
}

TEST_CASE("key_update_state receive same phase returns false") {
    key_update_state state;
    state.receive_key_phase(key_phase::phase_0);
    
    CHECK_FALSE(state.receive_key_phase(key_phase::phase_0));
}

TEST_CASE("key_update_state receive different phase returns true") {
    key_update_state state;
    state.receive_key_phase(key_phase::phase_0);
    
    CHECK(state.receive_key_phase(key_phase::phase_1));
    CHECK(state.current_phase() == key_phase::phase_1);
    CHECK(state.update_in_progress());
}

TEST_CASE("key_update_state reset returns to initial") {
    key_update_state state;
    state.receive_key_phase(key_phase::phase_0);
    state.initiate_update();
    
    state.reset();
    
    CHECK(state.current_phase() == key_phase::phase_0);
    CHECK_FALSE(state.update_in_progress());
}

TEST_CASE("key_update_manager initialize and get keys") {
    key_update_manager manager;
    
    traffic_key_material send_keys;
    send_keys.key = flowq::buffer{std::vector<std::byte>(16, std::byte{0x01})};
    send_keys.iv = flowq::buffer{std::vector<std::byte>(12, std::byte{0x02})};
    send_keys.header_protection_key = flowq::buffer{std::vector<std::byte>(16, std::byte{0x03})};
    send_keys.suite = cipher_suite::aes_128_gcm_sha256;
    
    traffic_key_material receive_keys;
    receive_keys.key = flowq::buffer{std::vector<std::byte>(16, std::byte{0x11})};
    receive_keys.iv = flowq::buffer{std::vector<std::byte>(12, std::byte{0x12})};
    receive_keys.header_protection_key = flowq::buffer{std::vector<std::byte>(16, std::byte{0x13})};
    receive_keys.suite = cipher_suite::aes_128_gcm_sha256;
    
    manager.initialize(key_phase::phase_0, std::move(send_keys), std::move(receive_keys),
                       cipher_suite::aes_128_gcm_sha256);
    
    CHECK(manager.current_phase() == key_phase::phase_0);
    CHECK(manager.current_send_keys() != nullptr);
    CHECK(manager.receive_keys_for_phase(key_phase::phase_0) != nullptr);
}

TEST_CASE("key_update_manager initiate update") {
    key_update_manager manager;
    
    traffic_key_material send_keys;
    send_keys.key = flowq::buffer{std::vector<std::byte>(16, std::byte{0x01})};
    send_keys.iv = flowq::buffer{std::vector<std::byte>(12, std::byte{0x02})};
    send_keys.header_protection_key = flowq::buffer{std::vector<std::byte>(16, std::byte{0x03})};
    send_keys.suite = cipher_suite::aes_128_gcm_sha256;
    
    traffic_key_material receive_keys;
    receive_keys.key = flowq::buffer{std::vector<std::byte>(16, std::byte{0x11})};
    receive_keys.iv = flowq::buffer{std::vector<std::byte>(12, std::byte{0x12})};
    receive_keys.header_protection_key = flowq::buffer{std::vector<std::byte>(16, std::byte{0x13})};
    receive_keys.suite = cipher_suite::aes_128_gcm_sha256;
    
    manager.initialize(key_phase::phase_0, std::move(send_keys), std::move(receive_keys),
                       cipher_suite::aes_128_gcm_sha256);
    
    // Receive a packet to enable update
    manager.receive_key_phase(key_phase::phase_0);
    
    auto new_phase = manager.initiate_update();
    REQUIRE(new_phase.has_value());
    CHECK(new_phase == key_phase::phase_1);
    CHECK(manager.current_phase() == key_phase::phase_1);
}

TEST_CASE("key_update_manager secure erase on destruction") {
    // This test verifies that the destructor doesn't crash
    // Actual zeroing verification would require memory inspection
    {
        key_update_manager manager;
        
        traffic_key_material send_keys;
        send_keys.key = flowq::buffer{std::vector<std::byte>(16, std::byte{0x01})};
        send_keys.iv = flowq::buffer{std::vector<std::byte>(12, std::byte{0x02})};
        send_keys.header_protection_key = flowq::buffer{std::vector<std::byte>(16, std::byte{0x03})};
        send_keys.suite = cipher_suite::aes_128_gcm_sha256;
        
        manager.initialize(key_phase::phase_0, std::move(send_keys), {},
                           cipher_suite::aes_128_gcm_sha256);
    }
    // If we get here without crashing, the test passes
    CHECK(true);
}
