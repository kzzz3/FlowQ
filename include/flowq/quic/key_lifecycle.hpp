#pragma once

#include <flowq/quic/ack_loss.hpp>
#include <flowq/quic/tls_handshake.hpp>

namespace flowq::quic {

/// Encryption levels corresponding to QUIC packet number spaces.
enum class encryption_level {
    initial,
    handshake,
    zero_rtt,
    one_rtt
};

/// Key direction: send or receive.
enum class key_direction {
    send,
    receive
};

/// Event describing key availability at a specific encryption level and direction.
struct key_availability_event {
    encryption_level level{};
    key_direction direction{};
};

/// Bidirectional key availability for a single encryption level.
struct directional_key_availability {
    bool send{};
    bool receive{};
};

/// Deterministic key lifecycle state machine.
/// Tracks key availability events and derives packet-space discard decisions.
/// Does not store, export, or install actual TLS secrets or key material.
class key_lifecycle_state {
public:
    void install(key_availability_event event) noexcept {
        auto& availability = availability_for(event.level);
        if (event.direction == key_direction::send) {
            availability.send = true;
        } else {
            availability.receive = true;
        }
    }

    [[nodiscard]] bool available(encryption_level level, key_direction direction) const noexcept {
        const auto& availability = availability_for(level);
        return direction == key_direction::send ? availability.send : availability.receive;
    }

    void observe_tls(handshake_state state, tls_key_availability keys) noexcept {
        if (keys.initial) {
            install_both(encryption_level::initial);
        }
        if (keys.handshake) {
            install_both(encryption_level::handshake);
            discard(packet_number_space::initial);
        }
        if (keys.application) {
            install_both(encryption_level::one_rtt);
        }
        if (state == handshake_state::handshake_confirmed) {
            discard(packet_number_space::handshake);
        }
    }

    void discard(packet_number_space space) noexcept {
        discarded_for(space) = true;
    }

    [[nodiscard]] bool discarded(packet_number_space space) const noexcept {
        return discarded_for(space);
    }

private:
    directional_key_availability initial_{};
    directional_key_availability handshake_{};
    directional_key_availability zero_rtt_{};
    directional_key_availability one_rtt_{};
    bool initial_discarded_{};
    bool handshake_discarded_{};
    bool application_discarded_{};

    void install_both(encryption_level level) noexcept {
        install(key_availability_event{level, key_direction::send});
        install(key_availability_event{level, key_direction::receive});
    }

    [[nodiscard]] directional_key_availability& availability_for(encryption_level level) noexcept {
        switch (level) {
        case encryption_level::initial:
            return initial_;
        case encryption_level::handshake:
            return handshake_;
        case encryption_level::zero_rtt:
            return zero_rtt_;
        case encryption_level::one_rtt:
            return one_rtt_;
        }
        return initial_;
    }

    [[nodiscard]] const directional_key_availability& availability_for(encryption_level level) const noexcept {
        switch (level) {
        case encryption_level::initial:
            return initial_;
        case encryption_level::handshake:
            return handshake_;
        case encryption_level::zero_rtt:
            return zero_rtt_;
        case encryption_level::one_rtt:
            return one_rtt_;
        }
        return initial_;
    }

    [[nodiscard]] bool& discarded_for(packet_number_space space) noexcept {
        if (space == packet_number_space::application) {
            return application_discarded_;
        }
        return space == packet_number_space::handshake ? handshake_discarded_ : initial_discarded_;
    }

    [[nodiscard]] bool discarded_for(packet_number_space space) const noexcept {
        if (space == packet_number_space::application) {
            return application_discarded_;
        }
        return space == packet_number_space::handshake ? handshake_discarded_ : initial_discarded_;
    }
};

} // namespace flowq::quic
