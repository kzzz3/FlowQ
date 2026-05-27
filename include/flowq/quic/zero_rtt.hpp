#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>
#include <flowq/quic/tls_handshake.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace flowq::quic {

/// 0-RTT encryption level for early data.
enum class zero_rtt_state {
    unavailable,    // 0-RTT not available
    capable,        // 0-RTT data can be sent
    accepted,       // 0-RTT accepted by server
    rejected,       // 0-RTT rejected by server
    early_data_sent // 0-RTT early data has been sent
};

/// 0-RTT configuration and state.
struct zero_rtt_config {
    bool enabled{false};
    std::uint64_t max_early_data_size{0};
    std::vector<std::byte> session_ticket;
};

/// 0-RTT early data result.
struct zero_rtt_result {
    zero_rtt_state state{zero_rtt_state::unavailable};
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }

    [[nodiscard]] bool can_send() const noexcept {
        return state == zero_rtt_state::capable && error.ok();
    }

    [[nodiscard]] bool is_accepted() const noexcept {
        return state == zero_rtt_state::accepted;
    }

    [[nodiscard]] bool is_rejected() const noexcept {
        return state == zero_rtt_state::rejected;
    }
};

/// 0-RTT manager: handles early data state and replay protection.
class zero_rtt_manager {
public:
    explicit zero_rtt_manager(zero_rtt_config config = {})
        : config_{config} {}

    /// Check if 0-RTT is available based on TLS state.
    [[nodiscard]] zero_rtt_result check_availability(const tls_handshake_adapter& adapter) const noexcept {
        if (!config_.enabled) {
            return {zero_rtt_state::unavailable, flowq::error{flowq::error_code::tls_error, "0-RTT disabled"}};
        }

        auto keys = adapter.key_availability();
        if (!keys.initial) {
            return {zero_rtt_state::unavailable, {}};
        }

        // 0-RTT is capable if we have initial keys and a session ticket
        if (!config_.session_ticket.empty()) {
            return {zero_rtt_state::capable, {}};
        }

        return {zero_rtt_state::unavailable, {}};
    }

    /// Record that 0-RTT early data has been sent.
    void record_early_data_sent() noexcept {
        state_ = zero_rtt_state::early_data_sent;
    }

    /// Record server's 0-RTT decision.
    void record_server_decision(bool accepted) noexcept {
        state_ = accepted ? zero_rtt_state::accepted : zero_rtt_state::rejected;
    }

    /// Get current 0-RTT state.
    [[nodiscard]] zero_rtt_state state() const noexcept {
        return state_;
    }

    /// Get 0-RTT configuration.
    [[nodiscard]] const zero_rtt_config& config() const noexcept {
        return config_;
    }

    /// Check if 0-RTT data should be retransmitted after rejection.
    [[nodiscard]] bool should_retransmit() const noexcept {
        return state_ == zero_rtt_state::rejected;
    }

private:
    zero_rtt_config config_;
    zero_rtt_state state_{zero_rtt_state::unavailable};
};

/// Anti-replay protection for 0-RTT.
/// Uses a sliding window to track received 0-RTT packet numbers.
class zero_rtt_replay_protection {
public:
    explicit zero_rtt_replay_protection(std::uint64_t window_size = 1024)
        : window_size_{window_size} {}

    /// Check if a 0-RTT packet number is potentially replayed.
    /// Returns true if the packet should be accepted (not a replay).
    [[nodiscard]] bool check_and_record(std::uint64_t packet_number) noexcept {
        if (largest_seen_ == 0 && bitmap_.empty()) {
            // First packet
            largest_seen_ = packet_number;
            bitmap_.resize(window_size_, false);
            bitmap_[packet_number % window_size_] = true;
            return true;
        }

        if (packet_number > largest_seen_) {
            // New largest - shift window
            auto shift = packet_number - largest_seen_;
            if (shift < window_size_) {
                // Shift bitmap
                for (std::size_t i = 0; i < window_size_ - shift; ++i) {
                    bitmap_[i] = bitmap_[i + shift];
                }
                for (std::size_t i = window_size_ - shift; i < window_size_; ++i) {
                    bitmap_[i] = false;
                }
            } else {
                // Complete reset
                std::fill(bitmap_.begin(), bitmap_.end(), false);
            }

            largest_seen_ = packet_number;
            bitmap_[packet_number % window_size_] = true;
            return true;
        }

        // Packet is <= largest_seen_
        if (largest_seen_ - packet_number >= window_size_) {
            return false;  // Too old
        }

        // Check the bitmap
        auto index = packet_number % window_size_;
        if (bitmap_[index]) {
            return false;  // Already seen
        }

        bitmap_[index] = true;
        return true;
    }

    /// Get the largest seen packet number.
    [[nodiscard]] std::uint64_t largest_seen() const noexcept {
        return largest_seen_;
    }

private:
    std::uint64_t window_size_;
    std::uint64_t largest_seen_{};
    std::vector<bool> bitmap_{};
};

} // namespace flowq::quic
