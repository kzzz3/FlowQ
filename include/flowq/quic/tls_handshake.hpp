#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>

#include <cstdint>
#include <vector>

namespace flowq::quic {

/// TLS encryption levels corresponding to QUIC packet number spaces.
enum class tls_encryption_level {
    initial,
    handshake,
    application
};

/// TLS handshake state machine states.
enum class handshake_state {
    idle,
    handshaking,
    handshake_confirmed,
    failed
};

/// Key availability for each encryption level.
struct tls_key_availability {
    bool initial{};
    bool handshake{};
    bool application{};
};

/// CRYPTO bytes at a specific encryption level and offset.
struct crypto_bytes {
    tls_encryption_level level{};
    std::uint64_t offset{};
    flowq::buffer data;
};

/// Abstract TLS handshake adapter boundary.
/// Provides opaque CRYPTO byte flow and handshake/key state observation.
/// External TLS implementations provide the concrete adapter.
class tls_handshake_adapter {
public:
    virtual ~tls_handshake_adapter() = default;

    /// Return the current handshake state.
    [[nodiscard]] virtual handshake_state state() const noexcept = 0;

    /// Return key availability for each encryption level.
    [[nodiscard]] virtual tls_key_availability key_availability() const noexcept = 0;

    /// Deliver inbound CRYPTO bytes to the TLS implementation.
    [[nodiscard]] virtual flowq::error receive_crypto(crypto_bytes bytes) = 0;

    /// Drain outbound CRYPTO bytes from the TLS implementation.
    [[nodiscard]] virtual std::vector<crypto_bytes> drain_crypto() = 0;
};

/// Check if application data can be sent: handshake confirmed AND application keys available.
[[nodiscard]] inline bool application_data_ready(const tls_handshake_adapter& adapter) noexcept {
    return adapter.state() == handshake_state::handshake_confirmed && adapter.key_availability().application;
}

} // namespace flowq::quic
