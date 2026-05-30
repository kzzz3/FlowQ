#pragma once

#include <flowq/quic/frame.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace flowq::quic {

enum class packet_number_space {
    initial,
    handshake,
    application
};

enum class sent_packet_state {
    outstanding,
    acknowledged,
    lost
};

struct sent_packet {
    packet_number_space space{};
    std::uint64_t packet_number{};
    bool ack_eliciting{};
    sent_packet_state state{sent_packet_state::outstanding};
};

struct loss_detection_result {
    std::vector<std::uint64_t> newly_acknowledged;
    std::vector<std::uint64_t> newly_lost;
};

namespace detail {

[[nodiscard]] inline std::chrono::steady_clock::duration scale_duration(
    std::chrono::steady_clock::duration duration,
    std::int64_t numerator,
    std::int64_t denominator) {
    return duration * numerator / denominator;
}

[[nodiscard]] inline std::chrono::steady_clock::duration abs_duration(std::chrono::steady_clock::duration duration) {
    return duration < std::chrono::steady_clock::duration::zero() ? -duration : duration;
}

[[nodiscard]] inline bool append_ack_range(std::set<std::uint64_t>& packets, std::uint64_t low, std::uint64_t high) {
    if (low > high) {
        return false;
    }

    for (auto packet = low; packet <= high; ++packet) {
        packets.insert(packet);
        if (packet == high) {
            break;
        }
    }
    return true;
}

[[nodiscard]] inline std::set<std::uint64_t> expand_ack_frame(const ack_frame& frame) {
    std::set<std::uint64_t> acknowledged;
    if (frame.first_ack_range > frame.largest_acknowledged) {
        return acknowledged;
    }

    auto range_high = frame.largest_acknowledged;
    auto range_low = frame.largest_acknowledged - frame.first_ack_range;
    if (!append_ack_range(acknowledged, range_low, range_high)) {
        return {};
    }

    for (const auto& range : frame.ranges) {
        const auto skipped = range.gap + 2;
        if (range_low < skipped) {
            return {};
        }

        range_high = range_low - skipped;
        if (range.length > range_high) {
            return {};
        }

        range_low = range_high - range.length;
        if (!append_ack_range(acknowledged, range_low, range_high)) {
            return {};
        }
    }

    return acknowledged;
}

} // namespace detail

struct rtt_sample {
    std::chrono::steady_clock::duration latest_rtt{};
    std::chrono::steady_clock::duration ack_delay{};
    std::chrono::steady_clock::duration peer_max_ack_delay{};
    bool handshake_confirmed{};
};

class rtt_estimator {
public:
    void update(const rtt_sample& sample) {
        latest_rtt_ = sample.latest_rtt;
        if (!has_sample_) {
            has_sample_ = true;
            min_rtt_ = sample.latest_rtt;
            smoothed_rtt_ = sample.latest_rtt;
            rtt_variance_ = sample.latest_rtt / 2;
            return;
        }

        min_rtt_ = std::min(min_rtt_, sample.latest_rtt);
        auto adjusted_rtt = sample.latest_rtt;
        if (sample.handshake_confirmed) {
            const auto ack_delay = std::min(sample.ack_delay, sample.peer_max_ack_delay);
            if (sample.latest_rtt - min_rtt_ > ack_delay) {
                adjusted_rtt -= ack_delay;
            }
        }

        rtt_variance_ = detail::scale_duration(rtt_variance_, 3, 4) + detail::scale_duration(detail::abs_duration(smoothed_rtt_ - adjusted_rtt), 1, 4);
        smoothed_rtt_ = detail::scale_duration(smoothed_rtt_, 7, 8) + detail::scale_duration(adjusted_rtt, 1, 8);
    }

    [[nodiscard]] bool has_sample() const noexcept {
        return has_sample_;
    }

    [[nodiscard]] std::chrono::steady_clock::duration latest_rtt() const noexcept {
        return latest_rtt_;
    }

    [[nodiscard]] std::chrono::steady_clock::duration min_rtt() const noexcept {
        return min_rtt_;
    }

    [[nodiscard]] std::chrono::steady_clock::duration smoothed_rtt() const noexcept {
        return smoothed_rtt_;
    }

    [[nodiscard]] std::chrono::steady_clock::duration rtt_variance() const noexcept {
        return rtt_variance_;
    }

private:
    bool has_sample_{};
    std::chrono::steady_clock::duration latest_rtt_{};
    std::chrono::steady_clock::duration min_rtt_{};
    std::chrono::steady_clock::duration smoothed_rtt_{};
    std::chrono::steady_clock::duration rtt_variance_{};
};

struct recovery_packet {
    packet_number_space space{};
    std::uint64_t packet_number{};
    std::chrono::steady_clock::time_point sent_at{};
    bool ack_eliciting{};
    sent_packet_state state{sent_packet_state::outstanding};
};

struct time_loss_result {
    std::vector<std::uint64_t> newly_lost;
    std::optional<std::chrono::steady_clock::time_point> earliest_loss_time;
};

struct pto_config {
    std::chrono::steady_clock::duration max_ack_delay{};
    std::chrono::steady_clock::duration initial_rtt{std::chrono::milliseconds{333}};
    std::uint32_t pto_count{};
    bool handshake_confirmed{};
};

enum class loss_timer_mode {
    none,
    loss_time,
    pto
};

struct loss_timer_deadline {
    loss_timer_mode mode{loss_timer_mode::none};
    std::optional<std::chrono::steady_clock::time_point> deadline;
};

[[nodiscard]] inline std::chrono::steady_clock::duration recovery_granularity() {
    return std::chrono::milliseconds{1};
}

[[nodiscard]] inline std::chrono::steady_clock::duration time_threshold_loss_delay(const rtt_estimator& estimator) {
    const auto rtt = std::max(estimator.latest_rtt(), estimator.smoothed_rtt());
    return std::max(detail::scale_duration(rtt, 9, 8), recovery_granularity());
}

[[nodiscard]] inline time_loss_result detect_time_threshold_losses(
    std::vector<recovery_packet>& packets,
    const rtt_estimator& estimator,
    packet_number_space space,
    std::uint64_t largest_acknowledged,
    std::chrono::steady_clock::time_point now) {
    time_loss_result result;
    if (!estimator.has_sample()) {
        return result;
    }

    const auto loss_delay = time_threshold_loss_delay(estimator);
    for (auto& packet : packets) {
        if (packet.space != space || packet.state != sent_packet_state::outstanding || !packet.ack_eliciting || packet.packet_number >= largest_acknowledged) {
            continue;
        }

        const auto loss_time = packet.sent_at + loss_delay;
        if (loss_time <= now) {
            packet.state = sent_packet_state::lost;
            result.newly_lost.push_back(packet.packet_number);
            continue;
        }

        if (!result.earliest_loss_time.has_value() || loss_time < *result.earliest_loss_time) {
            result.earliest_loss_time = loss_time;
        }
    }

    return result;
}

[[nodiscard]] inline bool pto_allowed(packet_number_space space, const pto_config& config) {
    return space != packet_number_space::application || config.handshake_confirmed;
}

[[nodiscard]] inline std::chrono::steady_clock::duration pto_duration(
    const rtt_estimator& estimator,
    packet_number_space space,
    const pto_config& config) {
    const auto smoothed_rtt = estimator.has_sample() ? estimator.smoothed_rtt() : config.initial_rtt;
    const auto rtt_variance = estimator.has_sample() ? estimator.rtt_variance() : config.initial_rtt / 2;
    const auto ack_delay = (space == packet_number_space::application && config.handshake_confirmed) ? config.max_ack_delay : std::chrono::steady_clock::duration{};
    auto duration = smoothed_rtt + std::max(rtt_variance * 4, recovery_granularity()) + ack_delay;
    for (std::uint32_t count = 0; count < config.pto_count; ++count) {
        duration *= 2;
    }
    return std::max(duration, recovery_granularity());
}

[[nodiscard]] inline std::chrono::steady_clock::time_point pto_deadline(
    std::chrono::steady_clock::time_point now,
    const rtt_estimator& estimator,
    packet_number_space space,
    const pto_config& config) {
    return now + pto_duration(estimator, space, config);
}

[[nodiscard]] inline loss_timer_deadline next_loss_timer(
    const std::vector<recovery_packet>& packets,
    const rtt_estimator& estimator,
    packet_number_space space,
    const pto_config& config) {
    std::optional<std::chrono::steady_clock::time_point> last_ack_eliciting_sent_at;
    std::optional<std::chrono::steady_clock::time_point> earliest_loss_time;

    const auto loss_delay = estimator.has_sample() ? time_threshold_loss_delay(estimator) : std::chrono::steady_clock::duration{};
    for (const auto& packet : packets) {
        if (packet.space != space || packet.state != sent_packet_state::outstanding || !packet.ack_eliciting) {
            continue;
        }

        if (!last_ack_eliciting_sent_at.has_value() || packet.sent_at > *last_ack_eliciting_sent_at) {
            last_ack_eliciting_sent_at = packet.sent_at;
        }

        if (estimator.has_sample()) {
            const auto loss_time = packet.sent_at + loss_delay;
            if (!earliest_loss_time.has_value() || loss_time < *earliest_loss_time) {
                earliest_loss_time = loss_time;
            }
        }
    }

    if (earliest_loss_time.has_value()) {
        return {loss_timer_mode::loss_time, earliest_loss_time};
    }

    if (last_ack_eliciting_sent_at.has_value() && pto_allowed(space, config)) {
        return {loss_timer_mode::pto, pto_deadline(*last_ack_eliciting_sent_at, estimator, space, config)};
    }

    return {};
}

class received_packet_tracker {
public:
    [[nodiscard]] bool observe(std::uint64_t packet_number) {
        return packets_.insert(packet_number).second;
    }

    [[nodiscard]] bool empty() const noexcept {
        return packets_.empty();
    }

    void clear() noexcept {
        packets_.clear();
    }

    [[nodiscard]] ack_frame to_ack_frame(std::uint64_t ack_delay = 0) const {
        if (packets_.empty()) {
            return {};
        }

        auto current_high = *packets_.rbegin();
        auto current_low = current_high;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;

        for (auto iterator = std::next(packets_.rbegin()); iterator != packets_.rend(); ++iterator) {
            const auto packet = *iterator;
            if (packet + 1 == current_low) {
                current_low = packet;
                continue;
            }

            ranges.emplace_back(current_low, current_high);
            current_high = packet;
            current_low = packet;
        }
        ranges.emplace_back(current_low, current_high);

        ack_frame frame{};
        frame.largest_acknowledged = ranges.front().second;
        frame.ack_delay = ack_delay;
        frame.first_ack_range = ranges.front().second - ranges.front().first;

        auto previous_low = ranges.front().first;
        for (std::size_t index = 1; index < ranges.size(); ++index) {
            const auto [low, high] = ranges[index];
            frame.ranges.push_back(ack_range{previous_low - high - 2, high - low});
            previous_low = low;
        }

        return frame;
    }

private:
    std::set<std::uint64_t> packets_{};
};

class sent_packet_tracker {
public:
    explicit sent_packet_tracker(packet_number_space space) : space_{space} {}

    void on_packet_sent(std::uint64_t packet_number, bool ack_eliciting) {
        packets_.push_back(sent_packet{space_, packet_number, ack_eliciting, sent_packet_state::outstanding});
    }

    [[nodiscard]] loss_detection_result on_ack_received(const ack_frame& frame, std::uint64_t packet_threshold = 3) {
        loss_detection_result result;
        const auto acknowledged = detail::expand_ack_frame(frame);
        if (acknowledged.empty()) {
            return result;
        }

        for (auto& packet : packets_) {
            if (acknowledged.contains(packet.packet_number) && packet.state == sent_packet_state::outstanding) {
                packet.state = sent_packet_state::acknowledged;
                result.newly_acknowledged.push_back(packet.packet_number);
            }
        }

        for (auto& packet : packets_) {
            if (packet.state != sent_packet_state::outstanding || !packet.ack_eliciting) {
                continue;
            }

            if (packet.packet_number <= frame.largest_acknowledged && frame.largest_acknowledged - packet.packet_number >= packet_threshold) {
                packet.state = sent_packet_state::lost;
                result.newly_lost.push_back(packet.packet_number);
            }
        }

        return result;
    }

    [[nodiscard]] const std::vector<sent_packet>& packets() const noexcept {
        return packets_;
    }

    void clear() noexcept {
        packets_.clear();
    }

    void mark_lost(std::uint64_t packet_number) noexcept {
        for (auto& packet : packets_) {
            if (packet.packet_number == packet_number && packet.state == sent_packet_state::outstanding) {
                packet.state = sent_packet_state::lost;
                return;
            }
        }
    }

private:
    packet_number_space space_{};
    std::vector<sent_packet> packets_{};
};

} // namespace flowq::quic
