#pragma once

#include <flowq/error.hpp>
#include <flowq/quic/connection_routing.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace flowq::quic {

struct endpoint_driver_config {
    std::size_t max_connections{100};
    std::size_t max_send_queue_size{1000};
};

class endpoint_driver {
public:
    explicit endpoint_driver(endpoint_driver_config config)
        : config_{config} {}

    void start() noexcept {
        running_ = true;
    }

    void stop() noexcept {
        running_ = false;
    }

    [[nodiscard]] bool running() const noexcept {
        return running_;
    }

    [[nodiscard]] const endpoint_driver_config& config() const noexcept {
        return config_;
    }

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

    void unregister_connection(const connection_id& cid) {
        routing_.retire(cid);
    }

    [[nodiscard]] std::optional<std::uint64_t> lookup_connection(const connection_id& cid) const {
        return routing_.lookup(cid);
    }

    [[nodiscard]] std::size_t connection_count() const noexcept {
        return routing_.active_count();
    }

private:
    endpoint_driver_config config_;
    routing_table routing_{};
    bool running_{};
};

} // namespace flowq::quic
