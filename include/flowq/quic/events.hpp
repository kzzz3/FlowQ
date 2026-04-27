#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>
#include <flowq/quic/core.hpp>

#include <cstdint>
#include <vector>

namespace flowq::quic {

struct session_stream_delivery {
    std::uint64_t stream_id{};
    flowq::buffer data;
    bool final_size_known{};
    std::uint64_t final_size{};
    bool closed{};
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

struct session_send_result {
    std::vector<outbound_datagram> datagrams;
    std::vector<close_action> closes;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

struct session_receive_result {
    std::vector<session_stream_delivery> stream_deliveries;
    std::vector<outbound_datagram> datagrams;
    std::vector<close_action> closes;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

} // namespace flowq::quic
