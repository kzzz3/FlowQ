#pragma once

#include <asio/io_context.hpp>

namespace flowq {

class context {
public:
    context() = default;

    [[nodiscard]] ::asio::io_context& io_context() noexcept {
        return io_context_;
    }

    [[nodiscard]] const ::asio::io_context& io_context() const noexcept {
        return io_context_;
    }

private:
    ::asio::io_context io_context_{};
};

} // namespace flowq
