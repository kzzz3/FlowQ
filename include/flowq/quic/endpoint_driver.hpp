#pragma once

#include <flowq/error.hpp>
#include <flowq/quic/connection_routing.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace flowq::quic {

struct endpoint_driver_config {
    std::size_t max_connections{100};
};

/// Production-shaped endpoint driver with explicit lifecycle, CID routing, and connection limits.
/// Provides a deterministic server-side connection management boundary.
/// @note This class is NOT thread-safe. All methods must be called from the same thread.
class endpoint_driver {
public:
    explicit endpoint_driver(endpoint_driver_config config)
        : config_{config} {}

    /// Start the endpoint driver. Must be called before registering connections.
    void start() noexcept {
        running_ = true;
    }

    /// Stop the endpoint driver. Idempotent - safe to call multiple times.
    void stop() noexcept {
        running_ = false;
    }

    /// Check if the endpoint driver is currently running.
    [[nodiscard]] bool running() const noexcept {
        return running_;
    }

    /// Return the current configuration.
    [[nodiscard]] const endpoint_driver_config& config() const noexcept {
        return config_;
    }

    /// Register a connection ID mapping. Returns error if not running or limit reached.
    [[nodiscard]] flowq::error register_connection(const connection_id& cid, std::uint64_t handle) {
        if (!running_) {
            return flowq::error{flowq::error_code::internal_error, "endpoint driver is not running"};
        }
        if (routing_.active_count() >= config_.max_connections) {
            return flowq::error{flowq::error_code::internal_error, "maximum connection limit reached"};
        }
        routing_.add(cid, handle);
        return {};
    }

    /// Unregister a connection ID, removing it from the routing table.
    void unregister_connection(const connection_id& cid) noexcept {
        routing_.retire(cid);
    }

    /// Look up a connection handle by destination connection ID.
    /// Returns nullopt if the CID is not registered.
    [[nodiscard]] std::optional<std::uint64_t> lookup_connection(const connection_id& cid) const noexcept {
        return routing_.lookup(cid);
    }

    /// Return the number of active connections.
    [[nodiscard]] std::size_t connection_count() const noexcept {
        return routing_.active_count();
    }

private:
    endpoint_driver_config config_;
    routing_table routing_{};
    bool running_{};
};

} // namespace flowq::quic
