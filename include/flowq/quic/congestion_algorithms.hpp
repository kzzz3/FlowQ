#pragma once

#include <flowq/quic/congestion.hpp>

#include <cstdint>
#include <memory>

namespace flowq::quic {

/// Congestion control algorithm type.
enum class congestion_algorithm {
    new_reno,
    bbr,
    cubic
};

/// Congestion control interface.
class congestion_control_interface {
public:
    virtual ~congestion_control_interface() = default;

    /// Called when a packet is sent.
    virtual void on_packet_sent(std::uint64_t bytes, bool ack_eliciting) noexcept = 0;

    /// Called when a packet is acknowledged.
    virtual void on_packet_acknowledged(std::uint64_t bytes) noexcept = 0;

    /// Called when a packet is lost.
    virtual void on_packet_lost(std::uint64_t bytes) noexcept = 0;

    /// Called when a congestion event occurs.
    virtual void on_congestion_event() noexcept = 0;

    /// Get current congestion window.
    [[nodiscard]] virtual std::uint64_t congestion_window() const noexcept = 0;

    /// Get bytes in flight.
    [[nodiscard]] virtual std::uint64_t bytes_in_flight() const noexcept = 0;

    /// Check if sending is allowed.
    [[nodiscard]] virtual bool can_send() const noexcept = 0;

    /// Get current congestion phase.
    [[nodiscard]] virtual congestion_phase state() const noexcept = 0;
};

/// NewReno congestion control adapter backed by FlowQ's QUIC recovery controller.
class new_reno_congestion_controller final : public congestion_control_interface {
public:
    explicit new_reno_congestion_controller() = default;

    void on_packet_sent(std::uint64_t bytes, bool ack_eliciting) noexcept override {
        controller_.on_packet_sent(bytes, ack_eliciting);
    }

    void on_packet_acknowledged(std::uint64_t bytes) noexcept override {
        controller_.on_packet_acknowledged(bytes);
    }

    void on_packet_lost(std::uint64_t bytes) noexcept override {
        controller_.on_packet_lost(bytes);
    }

    void on_congestion_event() noexcept override {
        controller_.on_congestion_event();
    }

    [[nodiscard]] std::uint64_t congestion_window() const noexcept override {
        return controller_.congestion_window();
    }

    [[nodiscard]] std::uint64_t bytes_in_flight() const noexcept override {
        return controller_.bytes_in_flight();
    }

    [[nodiscard]] bool can_send() const noexcept override {
        return controller_.can_send();
    }

    [[nodiscard]] congestion_phase state() const noexcept override {
        return controller_.state();
    }

private:
    congestion_controller controller_{};
};

/// BBR (Bottleneck Bandwidth and Round-trip propagation time) congestion control.
///
/// BBR is a model-based congestion control algorithm that estimates the
/// bottleneck bandwidth and minimum RTT to determine the optimal sending rate.
///
/// Reference: https://github.com/google/bbr
class bbr_congestion_controller final : public congestion_control_interface {
public:
    bbr_congestion_controller() = default;

    void on_packet_sent(std::uint64_t bytes, bool ack_eliciting) noexcept override {
        bytes_in_flight_ += bytes;
        total_sent_ += bytes;
    }

    void on_packet_acknowledged(std::uint64_t bytes) noexcept override {
        bytes_in_flight_ -= bytes;
        total_acked_ += bytes;
        
        // Update delivery rate estimation
        auto now = std::chrono::steady_clock::now();
        if (last_ack_time_.time_since_epoch().count() > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - last_ack_time_);
            if (elapsed.count() > 0) {
                delivery_rate_ = (bytes * 1000000) / elapsed.count();
            }
        }
        last_ack_time_ = now;
    }

    void on_packet_lost(std::uint64_t bytes) noexcept override {
        bytes_in_flight_ -= bytes;
        total_lost_ += bytes;
        
        // On loss, reduce sending rate
        cwnd_ = std::max(cwnd_ / 2, min_cwnd_);
        phase_ = congestion_phase::recovery;
    }

    void on_congestion_event() noexcept override {
        cwnd_ = std::max(cwnd_ / 2, min_cwnd_);
    }

    [[nodiscard]] std::uint64_t congestion_window() const noexcept override {
        return cwnd_;
    }

    [[nodiscard]] std::uint64_t bytes_in_flight() const noexcept override {
        return bytes_in_flight_;
    }

    [[nodiscard]] bool can_send() const noexcept override {
        return bytes_in_flight_ < cwnd_;
    }

    [[nodiscard]] congestion_phase state() const noexcept override {
        return phase_;
    }

    /// Get estimated bottleneck bandwidth (bytes/sec).
    [[nodiscard]] std::uint64_t bottleneck_bandwidth() const noexcept {
        return delivery_rate_;
    }

    /// Get minimum RTT.
    [[nodiscard]] std::chrono::microseconds min_rtt() const noexcept {
        return min_rtt_;
    }

private:
    std::uint64_t cwnd_{initial_cwnd_};
    std::uint64_t bytes_in_flight_{0};
    std::uint64_t total_sent_{0};
    std::uint64_t total_acked_{0};
    std::uint64_t total_lost_{0};
    std::uint64_t delivery_rate_{0};
    std::chrono::steady_clock::time_point last_ack_time_{};
    std::chrono::microseconds min_rtt_{std::chrono::milliseconds(100)};
    congestion_phase phase_{congestion_phase::slow_start};

    static constexpr std::uint64_t initial_cwnd_ = 10 * 1200;  // 10 * max_datagram_size
    static constexpr std::uint64_t min_cwnd_ = 2 * 1200;       // 2 * max_datagram_size
};

/// CUBIC congestion control.
///
/// CUBIC is a congestion control algorithm standardized in RFC 8312.
/// It uses a cubic function to calculate the congestion window.
///
/// Reference: RFC 8312
class cubic_congestion_controller final : public congestion_control_interface {
public:
    cubic_congestion_controller() = default;

    void on_packet_sent(std::uint64_t bytes, bool ack_eliciting) noexcept override {
        bytes_in_flight_ += bytes;
    }

    void on_packet_acknowledged(std::uint64_t bytes) noexcept override {
        bytes_in_flight_ -= bytes;
        
        if (phase_ == congestion_phase::slow_start) {
            // Slow start: increase cwnd by bytes_acked
            cwnd_ += bytes;
            if (cwnd_ >= ssthresh_) {
                phase_ = congestion_phase::congestion_avoidance;
                epoch_start_ = std::chrono::steady_clock::now();
                last_max_cwnd_ = cwnd_;
            }
        } else if (phase_ == congestion_phase::congestion_avoidance) {
            // CUBIC congestion avoidance
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - epoch_start_).count();
            
            // CUBIC function: W_cubic(t) = C * (t - K)^3 + W_max
            // where K = cbrt(W_max * beta / C)
            auto t = static_cast<double>(elapsed) / 1000.0;  // Convert to seconds
            auto w_max = static_cast<double>(last_max_cwnd_) / 1200.0;  // In segments
            auto k = std::cbrt(w_max * beta_ / cubic_c_);
            auto w_cubic = cubic_c_ * std::pow(t - k, 3.0) + w_max;
            
            cwnd_ = static_cast<std::uint64_t>(w_cubic * 1200.0);
            cwnd_ = std::max(cwnd_, min_cwnd_);
        }
    }

    void on_packet_lost(std::uint64_t bytes) noexcept override {
        bytes_in_flight_ -= bytes;
        
        last_max_cwnd_ = cwnd_;
        cwnd_ = static_cast<std::uint64_t>(cwnd_ * beta_);
        cwnd_ = std::max(cwnd_, min_cwnd_);
        ssthresh_ = cwnd_;
        phase_ = congestion_phase::recovery;
        
        epoch_start_ = std::chrono::steady_clock::now();
    }

    void on_congestion_event() noexcept override {
        last_max_cwnd_ = cwnd_;
        cwnd_ = static_cast<std::uint64_t>(cwnd_ * beta_);
        cwnd_ = std::max(cwnd_, min_cwnd_);
        ssthresh_ = cwnd_;
    }

    [[nodiscard]] std::uint64_t congestion_window() const noexcept override {
        return cwnd_;
    }

    [[nodiscard]] std::uint64_t bytes_in_flight() const noexcept override {
        return bytes_in_flight_;
    }

    [[nodiscard]] bool can_send() const noexcept override {
        return bytes_in_flight_ < cwnd_;
    }

    [[nodiscard]] congestion_phase state() const noexcept override {
        return phase_;
    }

private:
    std::uint64_t cwnd_{initial_cwnd_};
    std::uint64_t bytes_in_flight_{0};
    std::uint64_t ssthresh_{initial_cwnd_};
    std::uint64_t last_max_cwnd_{0};
    std::chrono::steady_clock::time_point epoch_start_{};
    congestion_phase phase_{congestion_phase::slow_start};

    // CUBIC parameters (RFC 8312)
    static constexpr double cubic_c_ = 0.4;
    static constexpr double beta_ = 0.7;
    
    static constexpr std::uint64_t initial_cwnd_ = 10 * 1200;  // 10 * max_datagram_size
    static constexpr std::uint64_t min_cwnd_ = 2 * 1200;       // 2 * max_datagram_size
};

/// Factory function to create a production congestion controller by algorithm type.
[[nodiscard]] inline std::unique_ptr<congestion_control_interface> create_congestion_controller(
    congestion_algorithm algorithm) {
    switch (algorithm) {
    case congestion_algorithm::new_reno:
        return std::make_unique<new_reno_congestion_controller>();
    case congestion_algorithm::bbr:
        return std::make_unique<bbr_congestion_controller>();
    case congestion_algorithm::cubic:
        return std::make_unique<cubic_congestion_controller>();
    }

    return nullptr;
}

} // namespace flowq::quic
