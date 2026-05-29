#pragma once

#include <flowq/quic/congestion.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <limits>

namespace flowq::quic {

/// Congestion control algorithm type.
enum class congestion_algorithm {
    new_reno,
    bbr,
    cubic
};

/// Congestion control interface.
/// Abstract base class for congestion control algorithms.
class congestion_control_interface {
public:
    virtual ~congestion_control_interface() = default;

    /// Called when a packet is sent.
    virtual void on_packet_sent(std::uint64_t bytes, bool ack_eliciting) = 0;

    /// Called when a packet is acknowledged.
    virtual void on_packet_acknowledged(std::uint64_t bytes) = 0;

    /// Called when a packet is lost.
    virtual void on_packet_lost(std::uint64_t bytes) = 0;

    /// Called when a congestion event occurs.
    virtual void on_congestion_event() = 0;

    /// Get current congestion window.
    [[nodiscard]] virtual std::uint64_t congestion_window() const noexcept = 0;

    /// Get bytes in flight.
    [[nodiscard]] virtual std::uint64_t bytes_in_flight() const noexcept = 0;

    /// Check if sending is allowed.
    [[nodiscard]] virtual bool can_send() const noexcept = 0;

    /// Get current congestion phase.
    [[nodiscard]] virtual congestion_phase state() const noexcept = 0;
};

/// BBR (Bottleneck Bandwidth and Round-trip propagation time) congestion control.
/// Implements a simplified version of BBR v1.
class bbr_congestion_controller : public congestion_control_interface {
public:
    explicit bbr_congestion_controller() = default;

    void on_packet_sent(std::uint64_t bytes, bool ack_eliciting) override {
        if (ack_eliciting) {
            bytes_in_flight_ += bytes;
        }
    }

    void on_packet_acknowledged(std::uint64_t bytes) override {
        if (bytes_in_flight_ >= bytes) {
            bytes_in_flight_ -= bytes;
        } else {
            bytes_in_flight_ = 0;
        }

        // Update delivery rate
        delivered_bytes_ += bytes;
        auto now = std::chrono::steady_clock::now();
        if (last_ack_time_.time_since_epoch().count() > 0) {
            auto interval = now - last_ack_time_;
            if (interval.count() > 0) {
                auto rate = (delivered_bytes_ * 1000000000LL) / interval.count();
                if (rate > bottleneck_bandwidth_) {
                    bottleneck_bandwidth_ = rate;
                }
            }
        }
        last_ack_time_ = now;

        // BBR uses bandwidth and RTT to compute cwnd
        if (rtt_estimator_.has_sample()) {
            auto bdp = bottleneck_bandwidth_ * rtt_estimator_.smoothed_rtt().count() / 1000000000LL;
            congestion_window_ = std::max(bdp, default_minimum_window());
        }
    }

    void on_packet_lost(std::uint64_t bytes) override {
        if (bytes_in_flight_ >= bytes) {
            bytes_in_flight_ -= bytes;
        } else {
            bytes_in_flight_ = 0;
        }
    }

    void on_congestion_event() override {
        // BBR doesn't halve cwnd on congestion, but reduces pacing rate
        // For simplicity, we reduce cwnd by 25%
        congestion_window_ = congestion_window_ * 3 / 4;
        congestion_window_ = std::max(congestion_window_, default_minimum_window());
    }

    [[nodiscard]] std::uint64_t congestion_window() const noexcept override {
        return congestion_window_;
    }

    [[nodiscard]] std::uint64_t bytes_in_flight() const noexcept override {
        return bytes_in_flight_;
    }

    [[nodiscard]] bool can_send() const noexcept override {
        return bytes_in_flight_ < congestion_window_;
    }

    [[nodiscard]] congestion_phase state() const noexcept override {
        return congestion_phase::slow_start;  // BBR doesn't use traditional phases
    }

    /// Update RTT estimator.
    void update_rtt(const rtt_sample& sample) {
        rtt_estimator_.update(sample);
    }

private:
    std::uint64_t bytes_in_flight_{};
    std::uint64_t congestion_window_{default_initial_window()};
    std::uint64_t bottleneck_bandwidth_{};
    std::uint64_t delivered_bytes_{};
    std::chrono::steady_clock::time_point last_ack_time_{};
    rtt_estimator rtt_estimator_{};
};

/// CUBIC congestion control.
/// Implements the CUBIC congestion control algorithm.
class cubic_congestion_controller : public congestion_control_interface {
public:
    explicit cubic_congestion_controller() = default;

    void on_packet_sent(std::uint64_t bytes, bool ack_eliciting) override {
        if (ack_eliciting) {
            bytes_in_flight_ += bytes;
        }
    }

    void on_packet_acknowledged(std::uint64_t bytes) override {
        if (bytes_in_flight_ >= bytes) {
            bytes_in_flight_ -= bytes;
        } else {
            bytes_in_flight_ = 0;
        }

        // CUBIC growth function
        if (phase_ == congestion_phase::slow_start) {
            // Slow start: exponential growth
            congestion_window_ += bytes;
            if (congestion_window_ >= ssthresh_) {
                phase_ = congestion_phase::congestion_avoidance;
                epoch_start_ = std::chrono::steady_clock::now();
                W_max_ = congestion_window_;
            }
        } else {
            // Congestion avoidance: CUBIC growth
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration<double>(now - epoch_start_).count();
            
            // CUBIC function: W(t) = C * (t - K)^3 + W_max
            // where K = cube_root(W_max * (1 - beta) / C)
            constexpr double C = 0.4;
            constexpr double beta = 0.7;
            
            auto K = std::cbrt(W_max_ * (1.0 - beta) / C);
            auto target = C * std::pow(elapsed - K, 3) + W_max_;
            
            if (target > congestion_window_) {
                congestion_window_ = static_cast<std::uint64_t>(target);
            }
        }
    }

    void on_packet_lost(std::uint64_t bytes) override {
        if (bytes_in_flight_ >= bytes) {
            bytes_in_flight_ -= bytes;
        } else {
            bytes_in_flight_ = 0;
        }
    }

    void on_congestion_event() override {
        // CUBIC: reduce window by beta factor
        constexpr double beta = 0.7;
        W_max_ = congestion_window_;
        congestion_window_ = static_cast<std::uint64_t>(congestion_window_ * beta);
        congestion_window_ = std::max(congestion_window_, default_minimum_window());
        ssthresh_ = congestion_window_;
        phase_ = congestion_phase::congestion_avoidance;
        epoch_start_ = std::chrono::steady_clock::now();
    }

    [[nodiscard]] std::uint64_t congestion_window() const noexcept override {
        return congestion_window_;
    }

    [[nodiscard]] std::uint64_t bytes_in_flight() const noexcept override {
        return bytes_in_flight_;
    }

    [[nodiscard]] bool can_send() const noexcept override {
        return bytes_in_flight_ < congestion_window_;
    }

    [[nodiscard]] congestion_phase state() const noexcept override {
        return phase_;
    }

private:
    std::uint64_t bytes_in_flight_{};
    std::uint64_t congestion_window_{default_initial_window()};
    std::uint64_t ssthresh_{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t W_max_{};
    std::chrono::steady_clock::time_point epoch_start_{};
    congestion_phase phase_{congestion_phase::slow_start};
};

/// Factory function to create congestion controller by algorithm type.
[[nodiscard]] inline std::unique_ptr<congestion_control_interface> create_congestion_controller(
    congestion_algorithm algorithm) {
    switch (algorithm) {
    case congestion_algorithm::new_reno:
        // Return nullptr to indicate using existing congestion_controller
        return nullptr;
    case congestion_algorithm::bbr:
        return std::make_unique<bbr_congestion_controller>();
    case congestion_algorithm::cubic:
        return std::make_unique<cubic_congestion_controller>();
    default:
        return nullptr;
    }
}

} // namespace flowq::quic
