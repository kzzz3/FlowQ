#pragma once

#include <flowq/quic/ack_loss.hpp>
#include <flowq/quic/frame.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace flowq::quic {

struct connection_packet_space_state {
    std::vector<frame> queue;
    std::uint64_t next_packet_number{};
    received_packet_tracker received;
    sent_packet_tracker sent;
    std::optional<std::uint64_t> largest_acknowledged;

    explicit connection_packet_space_state(packet_number_space space) : sent{space} {}
};

class connection_packet_spaces {
public:
    connection_packet_spaces()
        : initial_{packet_number_space::initial},
          handshake_{packet_number_space::handshake},
          application_{packet_number_space::application} {}

    [[nodiscard]] connection_packet_space_state& get(packet_number_space space) noexcept {
        if (space == packet_number_space::application) {
            return application_;
        }
        return space == packet_number_space::handshake ? handshake_ : initial_;
    }

    [[nodiscard]] const connection_packet_space_state& get(packet_number_space space) const noexcept {
        if (space == packet_number_space::application) {
            return application_;
        }
        return space == packet_number_space::handshake ? handshake_ : initial_;
    }

    void clear(packet_number_space space) {
        auto& state = get(space);
        state.queue.clear();
        state.received.clear();
        state.sent.clear();
        state.largest_acknowledged.reset();
    }

private:
    connection_packet_space_state initial_;
    connection_packet_space_state handshake_;
    connection_packet_space_state application_;
};

} // namespace flowq::quic
