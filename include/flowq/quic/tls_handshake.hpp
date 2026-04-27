#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>

#include <cstdint>
#include <vector>

namespace flowq::quic {

enum class tls_encryption_level {
    initial,
    handshake,
    application
};

enum class handshake_state {
    idle,
    handshaking,
    handshake_confirmed,
    failed
};

struct tls_key_availability {
    bool initial{};
    bool handshake{};
    bool application{};
};

struct crypto_bytes {
    tls_encryption_level level{};
    std::uint64_t offset{};
    flowq::buffer data;
};

class tls_handshake_adapter {
public:
    virtual ~tls_handshake_adapter() = default;

    [[nodiscard]] virtual handshake_state state() const noexcept = 0;
    [[nodiscard]] virtual tls_key_availability key_availability() const noexcept = 0;
    [[nodiscard]] virtual flowq::error receive_crypto(crypto_bytes bytes) = 0;
    [[nodiscard]] virtual std::vector<crypto_bytes> drain_crypto() = 0;
};

[[nodiscard]] inline bool application_data_ready(const tls_handshake_adapter& adapter) noexcept {
    return adapter.state() == handshake_state::handshake_confirmed && adapter.key_availability().application;
}

} // namespace flowq::quic
