#pragma once

#include <flowq/quic/key_derivation.hpp>
#include <flowq/quic/key_lifecycle.hpp>
#include <flowq/secure.hpp>

#include <cstdint>
#include <functional>

namespace flowq::quic {

/// Key phase for 1-RTT packets (RFC 9000 Section 6).
enum class key_phase : std::uint8_t {
    phase_0 = 0,  ///< Initial key phase
    phase_1 = 1   ///< Updated key phase
};

/// Key update state machine (RFC 9000 Section 6).
///
/// Manages the key phase bit and coordinates key material updates.
/// The key phase alternates between 0 and 1 with each key update.
///
/// Key update flow:
/// 1. Local initiator: derive new keys → install send keys → set key_phase bit
/// 2. Remote initiator: detect key_phase bit change → derive new keys → install receive keys
/// 3. After receiving ACK for packets with new key phase → discard old keys
class key_update_state {
public:
    key_update_state() = default;

    /// Get current key phase for sending.
    [[nodiscard]] key_phase current_phase() const noexcept {
        return current_phase_;
    }

    /// Check if a key update is in progress (waiting for ACK).
    [[nodiscard]] bool update_in_progress() const noexcept {
        return update_in_progress_;
    }

    /// Check if we can initiate a key update.
    /// A key update can be initiated when:
    /// - No update is currently in progress
    /// - We have received at least one packet with the current key phase
    [[nodiscard]] bool can_update() const noexcept {
        return !update_in_progress_ && received_current_phase_;
    }

    /// Initiate a key update (local side).
    /// Returns the new key phase for which new keys should be derived.
    /// @pre can_update() must be true
    [[nodiscard]] key_phase initiate_update() noexcept {
        // Toggle phase
        current_phase_ = (current_phase_ == key_phase::phase_0) 
            ? key_phase::phase_1 
            : key_phase::phase_0;
        update_in_progress_ = true;
        received_current_phase_ = false;
        return current_phase_;
    }

    /// Handle receiving a packet with a key phase bit.
    /// @param phase The key phase from the received packet
    /// @return true if this is a key phase change (new keys needed), false if same phase
    [[nodiscard]] bool receive_key_phase(key_phase phase) noexcept {
        received_current_phase_ = true;

        if (phase == current_phase_) {
            return false;  // Same phase, no update needed
        }

        // Key phase change detected
        if (update_in_progress_) {
            // We initiated an update and received a packet with the old phase
            // This is normal - remote hasn't seen our new phase yet
            return false;
        }

        // Remote initiated a key update
        current_phase_ = phase;
        update_in_progress_ = true;
        return true;  // New keys needed
    }

    /// Complete a key update after receiving ACK for packets with new phase.
    void complete_update() noexcept {
        update_in_progress_ = false;
    }

    /// Reset to initial state.
    void reset() noexcept {
        current_phase_ = key_phase::phase_0;
        update_in_progress_ = false;
        received_current_phase_ = false;
    }

private:
    key_phase current_phase_{key_phase::phase_0};
    bool update_in_progress_{false};
    bool received_current_phase_{false};
};

/// Key material for a specific key phase.
struct phased_key_material {
    key_phase phase{};
    traffic_key_material send_keys;
    traffic_key_material receive_keys;
    bool send_keys_active{false};
    bool receive_keys_active{false};

    /// Securely zero all key material.
    void secure_erase() noexcept {
        secure_zero_buffer(send_keys.key);
        secure_zero_buffer(send_keys.iv);
        secure_zero_buffer(send_keys.header_protection_key);
        secure_zero_buffer(receive_keys.key);
        secure_zero_buffer(receive_keys.iv);
        secure_zero_buffer(receive_keys.header_protection_key);
    }
};

/// Callback type for deriving new key material.
/// Takes a cipher suite and returns new key material.
using key_derivation_callback = std::function<traffic_key_material(cipher_suite suite)>;

/// Key update manager that coordinates key rotation.
///
/// Manages two sets of key material (phase 0 and phase 1) and handles
/// the key update lifecycle including:
/// - Initiating key updates
/// - Receiving key phase changes
/// - Installing new keys
/// - Discarding old keys after ACK confirmation
class key_update_manager {
public:
    key_update_manager() = default;

    ~key_update_manager() {
        secure_erase_all();
    }

    // Non-copyable for security
    key_update_manager(const key_update_manager&) = delete;
    key_update_manager& operator=(const key_update_manager&) = delete;

    // Movable
    key_update_manager(key_update_manager&& other) noexcept
        : state_{other.state_},
          current_keys_{std::move(other.current_keys_)},
          previous_keys_{std::move(other.previous_keys_)},
          cipher_suite_{other.cipher_suite_} {
        other.current_keys_ = nullptr;
        other.previous_keys_ = nullptr;
    }

    key_update_manager& operator=(key_update_manager&& other) noexcept {
        if (this != &other) {
            secure_erase_all();
            state_ = other.state_;
            current_keys_ = std::move(other.current_keys_);
            previous_keys_ = std::move(other.previous_keys_);
            cipher_suite_ = other.cipher_suite_;
            other.current_keys_ = nullptr;
            other.previous_keys_ = nullptr;
        }
        return *this;
    }

    /// Initialize with initial key material.
    void initialize(key_phase initial_phase, traffic_key_material send_keys, 
                    traffic_key_material receive_keys, cipher_suite suite) {
        cipher_suite_ = suite;
        auto& keys = get_or_create_keys(initial_phase);
        keys.phase = initial_phase;
        keys.send_keys = std::move(send_keys);
        keys.receive_keys = std::move(receive_keys);
        keys.send_keys_active = true;
        keys.receive_keys_active = true;
    }

    /// Get current key phase.
    [[nodiscard]] key_phase current_phase() const noexcept {
        return state_.current_phase();
    }

    /// Check if key update can be initiated.
    [[nodiscard]] bool can_update() const noexcept {
        return state_.can_update();
    }

    /// Initiate a key update.
    /// @return The new key phase, or nullopt if update cannot be initiated
    [[nodiscard]] std::optional<key_phase> initiate_update() {
        if (!can_update()) {
            return std::nullopt;
        }

        auto new_phase = state_.initiate_update();
        auto& keys = get_or_create_keys(new_phase);
        keys.phase = new_phase;
        // Keys will be derived and installed by the caller
        return new_phase;
    }

    /// Handle receiving a key phase from a packet.
    /// @param phase The key phase from the received packet
    /// @return true if new keys need to be derived (remote-initiated update)
    [[nodiscard]] bool receive_key_phase(key_phase phase) {
        return state_.receive_key_phase(phase);
    }

    /// Install send keys for a phase.
    void install_send_keys(key_phase phase, traffic_key_material keys) {
        auto& phased = get_or_create_keys(phase);
        phased.send_keys = std::move(keys);
        phased.send_keys_active = true;
    }

    /// Install receive keys for a phase.
    void install_receive_keys(key_phase phase, traffic_key_material keys) {
        auto& phased = get_or_create_keys(phase);
        phased.receive_keys = std::move(keys);
        phased.receive_keys_active = true;
    }

    /// Get send keys for current phase.
    [[nodiscard]] const traffic_key_material* current_send_keys() const noexcept {
        auto* keys = find_keys(state_.current_phase());
        return (keys && keys->send_keys_active) ? &keys->send_keys : nullptr;
    }

    /// Get receive keys for a specific phase.
    [[nodiscard]] const traffic_key_material* receive_keys_for_phase(key_phase phase) const noexcept {
        auto* keys = find_keys(phase);
        return (keys && keys->receive_keys_active) ? &keys->receive_keys : nullptr;
    }

    /// Complete a key update after ACK confirmation.
    void complete_update() noexcept {
        state_.complete_update();
        // Discard previous phase keys
        if (previous_keys_) {
            previous_keys_->secure_erase();
        }
    }

    /// Get cipher suite for key derivation.
    [[nodiscard]] cipher_suite get_cipher_suite() const noexcept {
        return cipher_suite_;
    }

private:
    key_update_state state_;
    phased_key_material* current_keys_{nullptr};
    phased_key_material* previous_keys_{nullptr};
    cipher_suite cipher_suite_{cipher_suite::unknown};

    phased_key_material& get_or_create_keys(key_phase phase) {
        if (current_keys_ && current_keys_->phase == phase) {
            return *current_keys_;
        }

        // Move current to previous
        if (current_keys_) {
            if (previous_keys_) {
                previous_keys_->secure_erase();
                delete previous_keys_;
            }
            previous_keys_ = current_keys_;
        }

        current_keys_ = new phased_key_material{};
        current_keys_->phase = phase;
        return *current_keys_;
    }

    phased_key_material* find_keys(key_phase phase) const {
        if (current_keys_ && current_keys_->phase == phase) {
            return current_keys_;
        }
        if (previous_keys_ && previous_keys_->phase == phase) {
            return previous_keys_;
        }
        return nullptr;
    }

    void secure_erase_all() noexcept {
        if (current_keys_) {
            current_keys_->secure_erase();
            delete current_keys_;
            current_keys_ = nullptr;
        }
        if (previous_keys_) {
            previous_keys_->secure_erase();
            delete previous_keys_;
            previous_keys_ = nullptr;
        }
    }
};

} // namespace flowq::quic
