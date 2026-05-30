#pragma once

#include <flowq/error.hpp>
#include <flowq/quic/key_derivation.hpp>
#include <flowq/quic/openssl_aead_protector.hpp>
#include <flowq/quic/openssl_tls_handshake.hpp>
#include <flowq/quic/tls_handshake.hpp>

#include <array>
#include <memory>
#include <optional>

namespace flowq::quic {

/// Result of creating protectors from a TLS handshake adapter.
struct tls_protector_set_result {
    flowq::error error{};
    bool ok() const noexcept { return error.ok(); }

    std::unique_ptr<openssl_aead_packet_protector> handshake_tx;
    std::unique_ptr<openssl_aead_packet_protector> handshake_rx;
    std::unique_ptr<openssl_aead_packet_protector> application_tx;
    std::unique_ptr<openssl_aead_packet_protector> application_rx;
};

/// Creates AEAD protectors from TLS handshake adapter traffic secrets.
/// This bridges the TLS adapter's secret export to the AEAD protector's key material.
class tls_protector_factory {
public:
    /// Create protectors from a TLS handshake adapter.
    /// The adapter must have completed the handshake and exported secrets.
    [[nodiscard]] static tls_protector_set_result create_protectors(
        openssl_tls_handshake_adapter& adapter) {

        tls_protector_set_result result;

        // Get negotiated cipher suite
        auto suite = adapter.negotiated_cipher();
        if (suite == cipher_suite::unknown) {
            result.error = flowq::error{flowq::error_code::tls_error, "unknown cipher suite"};
            return result;
        }

        // Create protectors for each level where secrets are available
        auto create_level_protector = [&](
            tls_encryption_level level, bool is_tx,
            std::unique_ptr<openssl_aead_packet_protector>& out) -> flowq::error {

            if (out != nullptr || !adapter.has_traffic_secret(level, is_tx)) {
                return {};
            }

            const auto& secret = adapter.traffic_secret(level, is_tx);
            auto material = derive_traffic_key_material(
                std::span<const std::byte>{secret.data(), secret.size()}, suite);

            if (!material.ok()) {
                return material.error;
            }

            auto protector = std::make_unique<openssl_aead_packet_protector>();
            auto create_result = openssl_aead_packet_protector::create(
                *protector, protection_for(level), std::move(material));

            if (!create_result.ok()) {
                return create_result.error;
            }

            out = std::move(protector);
            return {};
        };

        if (auto err = create_level_protector(tls_encryption_level::handshake, true, result.handshake_tx); !err.ok()) {
            result.error = err;
            return result;
        }
        if (auto err = create_level_protector(tls_encryption_level::handshake, false, result.handshake_rx); !err.ok()) {
            result.error = err;
            return result;
        }
        if (auto err = create_level_protector(tls_encryption_level::application, true, result.application_tx); !err.ok()) {
            result.error = err;
            return result;
        }
        if (auto err = create_level_protector(tls_encryption_level::application, false, result.application_rx); !err.ok()) {
            result.error = err;
            return result;
        }

        return result;
    }

private:
    [[nodiscard]] static protection_level protection_for(tls_encryption_level level) noexcept {
        return level == tls_encryption_level::application ? protection_level::application : protection_level::handshake;
    }
};

/// RAII wrapper for TLS protectors.
class tls_protector_set {
public:
    tls_protector_set() = default;
    ~tls_protector_set() = default;

    // Non-copyable
    tls_protector_set(const tls_protector_set&) = delete;
    tls_protector_set& operator=(const tls_protector_set&) = delete;

    // Movable
    tls_protector_set(tls_protector_set&& other) noexcept
        : protectors_{std::move(other.protectors_)} {}
    tls_protector_set& operator=(tls_protector_set&& other) noexcept = default;

    /// Create protectors from a TLS handshake adapter.
    [[nodiscard]] static tls_protector_set create(
        openssl_tls_handshake_adapter& adapter) {

        tls_protector_set set;
        set.protectors_ = tls_protector_factory::create_protectors(adapter);
        return set;
    }

    /// Check if creation was successful.
    [[nodiscard]] bool ok() const noexcept { return protectors_.ok(); }

    /// Get the error if creation failed.
    [[nodiscard]] const flowq::error& error() const noexcept { return protectors_.error; }

    /// Get protector for a specific level and direction.
    [[nodiscard]] const packet_protector* protector(
        packet_number_space space, bool is_tx) const noexcept {

        switch (space) {
        case packet_number_space::initial:
            return nullptr;
        case packet_number_space::handshake:
            return is_tx ? protectors_.handshake_tx.get() : protectors_.handshake_rx.get();
        case packet_number_space::application:
            return is_tx ? protectors_.application_tx.get() : protectors_.application_rx.get();
        }
        return nullptr;
    }

    /// Get the raw result for direct access.
    [[nodiscard]] const tls_protector_set_result& raw() const noexcept { return protectors_; }

private:
    tls_protector_set_result protectors_;
};

} // namespace flowq::quic
