#pragma once

#include <flowq/quic/frame.hpp>

#include <cstdint>
#include <iterator>
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

class received_packet_tracker {
public:
    [[nodiscard]] bool observe(std::uint64_t packet_number) {
        return packets_.insert(packet_number).second;
    }

    [[nodiscard]] bool empty() const noexcept {
        return packets_.empty();
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

private:
    packet_number_space space_{};
    std::vector<sent_packet> packets_{};
};

} // namespace flowq::quic
