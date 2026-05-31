#pragma once

#include <flowq/quic/congestion.hpp>

#include <cstdint>
#include <memory>

namespace flowq::quic {

/// Congestion control algorithm type.
enum class congestion_algorithm {
    new_reno
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

/// Factory function to create a production congestion controller by algorithm type.
[[nodiscard]] inline std::unique_ptr<congestion_control_interface> create_congestion_controller(
    congestion_algorithm algorithm) {
    switch (algorithm) {
    case congestion_algorithm::new_reno:
        return std::make_unique<new_reno_congestion_controller>();
    }

    return nullptr;
}

} // namespace flowq::quic
