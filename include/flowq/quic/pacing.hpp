#pragma once

#include <chrono>
#include <cstdint>
#include <algorithm>

namespace flowq::quic {

/// Pacing controller for smooth send rate.
///
/// Pacing spreads packet transmissions evenly over time to avoid burst losses.
/// It works with the congestion controller to determine the optimal send rate.
///
/// RFC 9002 Section 7.7: A sender SHOULD pace sending of all in-flight packets.
///
/// Design decisions:
/// - interval = cwnd / rtt (time between packets at current rate)
/// - Burst tolerance: allow short bursts up to BDP/4 or 10 packets
/// - Min interval: 100μs to prevent CPU spinning
/// - Max burst cap: 2x BDP to prevent network overload
/// - Slow start: pace at 2x rate (more aggressive)
/// - Congestion avoidance: pace at 1x rate (conservative)
class pacing_controller {
public:
    using clock = std::chrono::steady_clock;
    using duration = clock::duration;
    using time_point = clock::time_point;

    pacing_controller() = default;

    /// Initialize with congestion window and smoothed RTT.
    void initialize(std::uint64_t congestion_window, duration smoothed_rtt) noexcept {
        congestion_window_ = congestion_window;
        smoothed_rtt_ = smoothed_rtt;
        update_interval();
    }

    /// Update congestion window.
    void set_congestion_window(std::uint64_t congestion_window) noexcept {
        congestion_window_ = congestion_window;
        update_interval();
    }

    /// Update smoothed RTT.
    void set_smoothed_rtt(duration smoothed_rtt) noexcept {
        smoothed_rtt_ = smoothed_rtt;
        update_interval();
    }

    /// Set pacing gain (1.0 = normal, 2.0 = aggressive for slow start).
    void set_pacing_gain(double gain) noexcept {
        pacing_gain_ = gain;
        update_interval();
    }

    /// Check if we can send a packet now.
    /// @param bytes_in_flight Current bytes in flight
    /// @param packet_size Size of the packet to send
    [[nodiscard]] bool can_send(std::uint64_t bytes_in_flight, std::uint64_t packet_size) const noexcept {
        // Always allow if well below congestion window (burst tolerance)
        auto burst_threshold = std::min(congestion_window_ / 4, max_burst_bytes_);
        if (bytes_in_flight + packet_size <= burst_threshold) {
            return true;
        }

        // Check pacing timer
        auto now = clock::now();
        return now >= next_send_time_;
    }

    /// Record that a packet was sent.
    /// Updates the pacing timer for the next packet.
    void on_packet_sent(std::uint64_t packet_size) noexcept {
        auto now = clock::now();
        
        // Calculate effective interval with gain
        auto effective_interval = interval_;
        if (pacing_gain_ > 1.0) {
            effective_interval = duration(static_cast<long long>(
                interval_.count() / pacing_gain_));
        }

        if (now >= next_send_time_) {
            // We're on schedule or behind, send immediately next time
            next_send_time_ = now + effective_interval;
        } else {
            // We're ahead of schedule, wait for next slot
            next_send_time_ += effective_interval;
        }

        // Cap the send time to avoid falling too far behind
        auto max_behind = now - effective_interval * 4;
        if (next_send_time_ < max_behind) {
            next_send_time_ = max_behind;
        }
    }

    /// Get the time until the next packet can be sent.
    [[nodiscard]] duration time_until_next_send() const noexcept {
        auto now = clock::now();
        if (now >= next_send_time_) {
            return duration::zero();
        }
        return next_send_time_ - now;
    }

    /// Get the current pacing interval.
    [[nodiscard]] duration get_interval() const noexcept {
        return interval_;
    }

    /// Get the current pacing rate (bytes per second).
    [[nodiscard]] double get_rate_bps() const noexcept {
        if (interval_.count() == 0) {
            return 0.0;
        }
        // Rate = congestion_window / smoothed_rtt
        auto rtt_us = std::chrono::duration_cast<std::chrono::microseconds>(smoothed_rtt_).count();
        if (rtt_us == 0) {
            return 0.0;
        }
        return static_cast<double>(congestion_window_) * 1000000.0 / rtt_us;
    }

    /// Get the BDP (Bandwidth-Delay Product).
    [[nodiscard]] std::uint64_t bdp() const noexcept {
        auto rtt_us = std::chrono::duration_cast<std::chrono::microseconds>(smoothed_rtt_).count();
        if (rtt_us == 0) {
            return 0;
        }
        return congestion_window_;  // BDP ≈ cwnd at equilibrium
    }

    /// Reset the pacing controller.
    void reset() noexcept {
        congestion_window_ = 0;
        smoothed_rtt_ = duration::zero();
        interval_ = duration::zero();
        next_send_time_ = time_point::min();
        pacing_gain_ = 1.0;
    }

private:
    std::uint64_t congestion_window_{0};
    duration smoothed_rtt_{duration::zero()};
    duration interval_{duration::zero()};
    time_point next_send_time_{time_point::min()};
    double pacing_gain_{1.0};

    // Tuning constants
    static constexpr std::uint64_t max_datagram_size_{1200};
    static constexpr std::uint64_t max_burst_bytes_{12000};  // 10 packets
    static constexpr std::chrono::microseconds min_interval_{50};  // 50μs minimum (faster recovery for high-bandwidth)
    static constexpr std::chrono::milliseconds max_interval_{100};  // 100ms maximum

    /// Update the pacing interval based on congestion window and RTT.
    /// interval = cwnd * mss / rtt
    void update_interval() noexcept {
        if (congestion_window_ == 0 || smoothed_rtt_.count() == 0) {
            interval_ = duration::zero();
            return;
        }

        auto rtt_us = std::chrono::duration_cast<std::chrono::microseconds>(smoothed_rtt_).count();
        if (rtt_us == 0) {
            interval_ = duration::zero();
            return;
        }

        // interval = cwnd * mss / rtt (in microseconds)
        // Simplified: interval = cwnd / rtt * mss
        // To avoid overflow: interval_us = cwnd * mss / rtt_us
        auto interval_us = (congestion_window_ * max_datagram_size_) / rtt_us;
        
        // Apply min/max bounds
        auto interval = std::chrono::microseconds(interval_us);
        if (interval < min_interval_) interval = min_interval_;
        if (interval > max_interval_) interval = std::chrono::duration_cast<std::chrono::microseconds>(max_interval_);
        
        interval_ = std::chrono::duration_cast<duration>(interval);
    }
};

/// Burst absorber for handling packet bursts.
///
/// Allows short bursts while maintaining long-term pacing.
/// This is useful for handling ACK bursts or retransmissions.
class burst_absorber {
public:
    /// Check if a burst of the given size is allowed.
    /// @param burst_size Number of packets in the burst
    /// @param max_burst Maximum allowed burst size
    [[nodiscard]] bool allow_burst(std::uint64_t burst_size, std::uint64_t max_burst = 10) const noexcept {
        return burst_size <= max_burst;
    }

    /// Record a burst.
    void on_burst(std::uint64_t burst_size) noexcept {
        total_bursts_++;
        total_packets_burst_ += burst_size;
        max_burst_seen_ = std::max(max_burst_seen_, burst_size);
    }

    /// Get burst statistics.
    [[nodiscard]] std::uint64_t total_bursts() const noexcept { return total_bursts_; }
    [[nodiscard]] std::uint64_t total_packets_burst() const noexcept { return total_packets_burst_; }
    [[nodiscard]] std::uint64_t max_burst_seen() const noexcept { return max_burst_seen_; }

private:
    std::uint64_t total_bursts_{0};
    std::uint64_t total_packets_burst_{0};
    std::uint64_t max_burst_seen_{0};
};

} // namespace flowq::quic
