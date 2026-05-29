#pragma once

#include <string>
#include <utility>

namespace flowq {

enum class error_code {
    none,
    /// @reserved For Asio operation cancellation.
    cancelled,
    timeout,
    udp_error,
    tls_error,
    protocol_error,
    connection_closed,
    /// @reserved For future stream reset error handling.
    stream_reset,
    flow_control_error,
    internal_error
};

class error {
public:
    error() = default;

    error(error_code code, std::string message)
        : code_{code}, message_{std::move(message)} {}

    [[nodiscard]] error_code code() const noexcept {
        return code_;
    }

    [[nodiscard]] const std::string& message() const noexcept {
        return message_;
    }

    [[nodiscard]] bool ok() const noexcept {
        return code_ == error_code::none;
    }

private:
    error_code code_{error_code::none};
    std::string message_{};
};

} // namespace flowq
