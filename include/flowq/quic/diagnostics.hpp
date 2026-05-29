#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace flowq::quic {

/// Diagnostic event types for qlog-style observability.
enum class diagnostic_event_type {
    packet_sent,
    packet_received,
    packet_lost,
    key_updated,
    congestion_state_changed,
    transport_parameter_decoded
};

/// A single diagnostic event with timestamp and structured data.
struct diagnostic_event {
    diagnostic_event_type type{};
    std::chrono::steady_clock::time_point timestamp{};
    std::string category;
    std::string message;
};

/// Abstract sink for diagnostic events. Implement to collect or forward events.
/// Lifetime: the diagnostics object must not outlive its sink.
class diagnostic_sink {
public:
    virtual ~diagnostic_sink() = default;

    /// Emit a diagnostic event to this sink.
    virtual void emit(diagnostic_event event) = 0;
};

/// Concrete sink that collects events in memory for testing/inspection.
class diagnostic_collector final : public diagnostic_sink {
public:
    void emit(diagnostic_event event) override {
        events_.push_back(std::move(event));
    }

    /// Return all collected events.
    [[nodiscard]] const std::vector<diagnostic_event>& events() const noexcept {
        return events_;
    }

    [[nodiscard]] std::size_t count() const noexcept {
        return events_.size();
    }

    void clear() noexcept {
        events_.clear();
    }

private:
    std::vector<diagnostic_event> events_{};
};

class null_diagnostic_sink final : public diagnostic_sink {
public:
    void emit(diagnostic_event /*event*/) override {
        // No-op: diagnostics disabled
    }
};

/// @note The sink pointer is read without synchronization. Ensure the sink outlives this instance and is not modified concurrently.
class diagnostics {
public:
    /// @pre If non-null, the sink must outlive this diagnostics instance.
    explicit diagnostics(diagnostic_sink* sink = nullptr) : sink_{sink} {}

    /// @pre If non-null, the sink must outlive this diagnostics instance.
    void set_sink(diagnostic_sink* sink) noexcept {
        sink_ = sink;
    }

    [[nodiscard]] bool enabled() const noexcept {
        return sink_ != nullptr;
    }

    void packet_sent(std::uint64_t packet_number, std::size_t bytes) {
        if (sink_ != nullptr) [[likely]] {
            std::string msg;
            msg.reserve(32);
            msg.append("packet_sent pn=");
            msg.append(std::to_string(packet_number));
            msg.append(" bytes=");
            msg.append(std::to_string(bytes));
            sink_->emit(diagnostic_event{
                diagnostic_event_type::packet_sent,
                std::chrono::steady_clock::now(),
                "transport",
                std::move(msg)
            });
        }
    }

    void packet_received(std::uint64_t packet_number, std::size_t bytes) {
        if (sink_ != nullptr) [[likely]] {
            std::string msg;
            msg.reserve(36);
            msg.append("packet_received pn=");
            msg.append(std::to_string(packet_number));
            msg.append(" bytes=");
            msg.append(std::to_string(bytes));
            sink_->emit(diagnostic_event{
                diagnostic_event_type::packet_received,
                std::chrono::steady_clock::now(),
                "transport",
                std::move(msg)
            });
        }
    }

    void packet_lost(std::uint64_t packet_number) {
        if (sink_ != nullptr) [[likely]] {
            std::string msg;
            msg.reserve(20);
            msg.append("packet_lost pn=");
            msg.append(std::to_string(packet_number));
            sink_->emit(diagnostic_event{
                diagnostic_event_type::packet_lost,
                std::chrono::steady_clock::now(),
                "recovery",
                std::move(msg)
            });
        }
    }

    void key_updated(const std::string& level) {
        if (sink_ != nullptr) [[likely]] {
            std::string msg;
            msg.reserve(16 + level.size());
            msg.append("key_updated level=");
            msg.append(level);
            sink_->emit(diagnostic_event{
                diagnostic_event_type::key_updated,
                std::chrono::steady_clock::now(),
                "security",
                std::move(msg)
            });
        }
    }

    void congestion_state_changed(const std::string& old_state, const std::string& new_state) {
        if (sink_ != nullptr) [[likely]] {
            std::string msg;
            msg.reserve(28 + old_state.size() + new_state.size());
            msg.append("congestion_state_changed ");
            msg.append(old_state);
            msg.append(" -> ");
            msg.append(new_state);
            sink_->emit(diagnostic_event{
                diagnostic_event_type::congestion_state_changed,
                std::chrono::steady_clock::now(),
                "congestion",
                std::move(msg)
            });
        }
    }

    void transport_parameter_decoded(const std::string& name) {
        if (sink_ != nullptr) [[likely]] {
            std::string msg;
            msg.reserve(28 + name.size());
            msg.append("transport_parameter_decoded name=");
            msg.append(name);
            sink_->emit(diagnostic_event{
                diagnostic_event_type::transport_parameter_decoded,
                std::chrono::steady_clock::now(),
                "transport",
                std::move(msg)
            });
        }
    }

private:
    /// @pre Non-owning pointer; must outlive this instance.
    diagnostic_sink* sink_{};
};

} // namespace flowq::quic
