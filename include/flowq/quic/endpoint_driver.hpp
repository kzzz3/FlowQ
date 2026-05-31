#pragma once

#include <flowq/error.hpp>
#include <flowq/quic/connection_routing.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace flowq::quic {

struct endpoint_driver_config {
    std::size_t max_connections{100};
    stateless_reset_packet_config stateless_reset{};
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
        return register_connection_impl(cid, handle, std::nullopt);
    }

    /// Register a connection ID with its stateless reset token for later retirement handling.
    [[nodiscard]] flowq::error register_connection(
        const connection_id& cid,
        std::uint64_t handle,
        stateless_reset_token reset_token) {
        return register_connection_impl(cid, handle, reset_token);
    }

    [[nodiscard]] stateless_reset_packet_result build_stateless_reset(
        const connection_id& cid,
        std::size_t triggering_datagram_size) const {
        if (!running_) {
            return {{}, flowq::error{flowq::error_code::internal_error, "endpoint driver is not running"}};
        }
        if (routing_.lookup(cid).has_value()) {
            return {{}, flowq::error{flowq::error_code::protocol_error, "connection ID is still active"}};
        }
        const auto token = routing_.lookup_stateless_reset_token(cid);
        if (!token.has_value()) {
            return {{}, flowq::error{flowq::error_code::protocol_error, "stateless reset token is not registered"}};
        }
        return build_stateless_reset_packet(*token, triggering_datagram_size, config_.stateless_reset);
    }

    /// Unregister a connection ID, removing it from the routing table.
    void unregister_connection(const connection_id& cid) {
        routing_.retire(cid);
    }

    /// Look up a connection handle by destination connection ID.
    /// Returns nullopt if the CID is not registered.
    [[nodiscard]] std::optional<std::uint64_t> lookup_connection(const connection_id& cid) const {
        return routing_.lookup(cid);
    }

    /// Return the number of active connections.
    [[nodiscard]] std::size_t connection_count() const noexcept {
        return routing_.active_count();
    }

private:
    [[nodiscard]] flowq::error register_connection_impl(
        const connection_id& cid,
        std::uint64_t handle,
        std::optional<stateless_reset_token> reset_token) {
        if (!running_) {
            return flowq::error{flowq::error_code::internal_error, "endpoint driver is not running"};
        }
        if (routing_.active_count() >= config_.max_connections) {
            return flowq::error{flowq::error_code::internal_error, "maximum connection limit reached"};
        }
        routing_.add(cid, handle);
        if (reset_token.has_value()) {
            routing_.add_stateless_reset_token(cid, *reset_token);
        }
        return {};
    }

    endpoint_driver_config config_;
    routing_table routing_{};
    bool running_{};
};

} // namespace flowq::quic
