#pragma once

#include <flowq/quic/ack_loss.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

namespace flowq::quic {

enum class congestion_phase {
    slow_start,
    congestion_avoidance
};

struct congestion_packet {
    std::uint64_t packet_number{};
    std::chrono::steady_clock::time_point sent_at{};
    bool ack_eliciting{};
    std::uint64_t bytes{};
};

[[nodiscard]] inline std::uint64_t default_initial_window() noexcept {
    return 10 * 1200;
}

[[nodiscard]] inline std::uint64_t default_minimum_window() noexcept {
    return 2 * 1200;
}

[[nodiscard]] inline std::chrono::steady_clock::duration persistent_congestion_threshold(
    const rtt_estimator& estimator,
    std::chrono::steady_clock::duration max_ack_delay) noexcept {
    const auto rtt = estimator.has_sample()
        ? (3 * estimator.smoothed_rtt()) + (4 * estimator.rtt_variance())
        : std::chrono::milliseconds{333};
    const auto minimum = std::chrono::steady_clock::duration{std::chrono::milliseconds{100}};
    return std::max(rtt, minimum) + max_ack_delay;
}

class congestion_controller {
public:
    explicit congestion_controller() = default;

    [[nodiscard]] std::uint64_t bytes_in_flight() const noexcept {
        return bytes_in_flight_;
    }

    [[nodiscard]] std::uint64_t congestion_window() const noexcept {
        return congestion_window_;
    }

    [[nodiscard]] std::uint64_t slow_start_threshold() const noexcept {
        return ssthresh_;
    }

    [[nodiscard]] congestion_phase state() const noexcept {
        return phase_;
    }

    [[nodiscard]] bool can_send() const noexcept {
        return bytes_in_flight_ < congestion_window_;
    }

    void set_slow_start_threshold(std::uint64_t value) noexcept {
        ssthresh_ = value;
    }

    void enter_congestion_avoidance() noexcept {
        phase_ = congestion_phase::congestion_avoidance;
    }

    void update_rtt(const rtt_sample& sample) noexcept {
        rtt_.update(sample);
    }

    void on_packet_sent(std::uint64_t bytes, bool ack_eliciting) noexcept {
        if (ack_eliciting) {
            bytes_in_flight_ += bytes;
        }
    }

    void on_packet_acknowledged(std::uint64_t bytes) noexcept {
        if (bytes_in_flight_ >= bytes) {
            bytes_in_flight_ -= bytes;
        } else {
            bytes_in_flight_ = 0;
        }

        if (phase_ == congestion_phase::slow_start) {
            congestion_window_ += bytes;
        } else {
            // NewReno congestion avoidance: cwnd += MSS * acked / cwnd
            const auto mss = std::uint64_t{1200};
            congestion_window_ += (mss * bytes) / congestion_window_;
        }

        if (phase_ == congestion_phase::slow_start && congestion_window_ >= ssthresh_) {
            phase_ = congestion_phase::congestion_avoidance;
        }
    }

    void on_packet_lost(std::uint64_t bytes) noexcept {
        if (bytes_in_flight_ >= bytes) {
            bytes_in_flight_ -= bytes;
        } else {
            bytes_in_flight_ = 0;
        }
    }

    void on_congestion_event() noexcept {
        ssthresh_ = std::max(congestion_window_ / 2, default_minimum_window());
        congestion_window_ = ssthresh_;
        phase_ = congestion_phase::congestion_avoidance;
    }

    void on_persistent_congestion() noexcept {
        congestion_window_ = default_minimum_window();
        ssthresh_ = default_minimum_window();
        phase_ = congestion_phase::slow_start;
    }

    [[nodiscard]] bool detect_persistent_congestion(
        const std::vector<congestion_packet>& packets,
        std::chrono::steady_clock::time_point now) const {
        if (packets.size() < 2) {
            return false;
        }

        const auto threshold = persistent_congestion_threshold(rtt_, std::chrono::milliseconds{0});
        const auto last_sent = packets.back().sent_at;
        const auto idle_gap = now - last_sent;

        if (idle_gap < threshold) {
            return false;
        }

        // Verify consecutive ack-eliciting packets span the congestion period
        for (std::size_t i = 1; i < packets.size(); ++i) {
            if (!packets[i - 1].ack_eliciting || !packets[i].ack_eliciting) {
                continue;
            }
            const auto pair_gap = packets[i].sent_at - packets[i - 1].sent_at;
            if (pair_gap >= threshold) {
                return true;
            }
        }

        return idle_gap >= threshold;
    }

private:
    std::uint64_t bytes_in_flight_{};
    std::uint64_t congestion_window_{default_initial_window()};
    std::uint64_t ssthresh_{UINT64_MAX};
    congestion_phase phase_{congestion_phase::slow_start};
    rtt_estimator rtt_{};
};

} // namespace flowq::quic
