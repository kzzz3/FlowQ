#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>
#include <flowq/quic/http3.hpp>
#include <flowq/quic/http3_request.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace flowq::quic::http3 {

// Backward-compatible aliases for the canonical request/response types from http3_request.hpp.
using server_request = request;
using server_response = response;

/// HTTP/3 request handler function type.
using request_handler = std::function<response(const request&)>;

/// HTTP/3 server configuration.
struct server_config {
    std::string host;
    std::uint16_t port{};
    std::uint64_t max_connections{100};
    std::uint64_t max_streams_bidi{10};
    std::uint64_t max_streams_uni{10};
};

/// HTTP/3 server error.
[[nodiscard]] inline flowq::error server_error(const char* message) {
    return flowq::error{flowq::error_code::internal_error, message};
}

/// HTTP/3 server result.
struct server_result {
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

/// @warning This is a structural stub for testing only. NOT production-ready.
/// HTTP/3 server.
/// Handles HTTP/3 requests and generates responses.
class server {
public:
    explicit server(server_config config)
        : config_{std::move(config)} {}

    /// Register a request handler for a specific path.
    void on_request(const std::string& path, request_handler handler) {
        handlers_[path] = std::move(handler);
    }

    /// Start the server.
    [[nodiscard]] server_result start() {
        if (running_) {
            return {server_error("Server already running")};
        }

        // In a real implementation, this would:
        // 1. Create HTTP/3 listener
        // 2. Bind to host:port
        // 3. Start accepting connections

        running_ = true;
        return {};
    }

    /// Stop the server.
    [[nodiscard]] server_result stop() {
        if (!running_) {
            return {server_error("Server not running")};
        }

        // In a real implementation, this would:
        // 1. Close all connections
        // 2. Stop accepting new connections
        // 3. Clean up resources

        running_ = false;
        return {};
    }

    /// Process an incoming HTTP/3 request.
    [[nodiscard]] response handle_request(const request& req) {
        // Find handler for path
        auto it = handlers_.find(req.path);
        if (it != handlers_.end()) {
            return it->second(req);
        }

        // Default 404 response
        response result{};
        result.status_code = 404;
        result.headers["content-type"] = "text/plain";
        result.body = flowq::buffer{std::vector<std::byte>{
            std::byte{0x4e}, std::byte{0x6f}, std::byte{0x74}, std::byte{0x20}, std::byte{0x46}, std::byte{0x6f}, std::byte{0x75}, std::byte{0x6e}, std::byte{0x64}  // "Not Found"
        }};
        return result;
    }

    /// Get server configuration.
    [[nodiscard]] const server_config& config() const noexcept {
        return config_;
    }

    /// Check if server is running.
    [[nodiscard]] bool running() const noexcept {
        return running_;
    }

    /// Get number of registered handlers.
    [[nodiscard]] std::size_t handler_count() const noexcept {
        return handlers_.size();
    }

private:
    server_config config_;
    std::unordered_map<std::string, request_handler> handlers_;
    bool running_{};
};

/// HTTP/3 server builder.
class server_builder {
public:
    server_builder& host(const std::string& host) {
        config_.host = host;
        return *this;
    }

    server_builder& port(std::uint16_t port) {
        config_.port = port;
        return *this;
    }

    server_builder& max_connections(std::uint64_t max) {
        config_.max_connections = max;
        return *this;
    }

    server_builder& max_streams_bidi(std::uint64_t max) {
        config_.max_streams_bidi = max;
        return *this;
    }

    server_builder& max_streams_uni(std::uint64_t max) {
        config_.max_streams_uni = max;
        return *this;
    }

    [[nodiscard]] server build() {
        return server{config_};
    }

private:
    server_config config_;
};

} // namespace flowq::quic::http3
