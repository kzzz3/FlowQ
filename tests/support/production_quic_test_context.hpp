#pragma once

#include <flowq/error.hpp>
#include <flowq/quic/crypto_provider.hpp>
#include <flowq/quic/key_derivation.hpp>
#include <flowq/quic/key_lifecycle.hpp>
#include <flowq/quic/openssl_aead_protector.hpp>
#include <flowq/quic/tls_handshake.hpp>

#include <cstddef>
#include <utility>
#include <vector>

namespace flowq::quic::test {

class confirmed_tls_adapter final : public tls_handshake_adapter {
public:
    [[nodiscard]] handshake_state state() const noexcept override {
        return handshake_state::handshake_confirmed;
    }

    [[nodiscard]] tls_key_availability key_availability() const noexcept override {
        return tls_key_availability{true, true, true};
    }

    [[nodiscard]] crypto_provider_status provider_status() const noexcept override {
        return crypto_provider_status::available(
            cipher_suite::aes_128_gcm_sha256,
            crypto_capabilities{true, true, true, true, true});
    }

    [[nodiscard]] flowq::error receive_crypto(crypto_bytes bytes) override {
        received_.push_back(std::move(bytes));
        return {};
    }

    [[nodiscard]] std::vector<crypto_bytes> drain_crypto() override {
        return {};
    }

    [[nodiscard]] flowq::error advance() override {
        return {};
    }

private:
    std::vector<crypto_bytes> received_{};
};

struct production_packet_protectors {
    openssl_aead_packet_protector client_initial_tx;
    openssl_aead_packet_protector server_initial_tx;
    openssl_aead_packet_protector client_handshake_tx;
    openssl_aead_packet_protector server_handshake_tx;
    openssl_aead_packet_protector client_application_tx;
    openssl_aead_packet_protector server_application_tx;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

[[nodiscard]] inline std::vector<std::byte> traffic_secret(std::byte domain) {
    std::vector<std::byte> secret(32);
    for (std::size_t index = 0; index < secret.size(); ++index) {
        secret[index] = static_cast<std::byte>(
            (static_cast<unsigned int>(domain) + static_cast<unsigned int>(index * 17U)) & 0xffU);
    }
    return secret;
}

[[nodiscard]] inline flowq::error install_protector(
    openssl_aead_packet_protector& output,
    protection_level level,
    std::byte secret_domain) {
    auto material = derive_traffic_key_material(traffic_secret(secret_domain), cipher_suite::aes_128_gcm_sha256);
    if (!material.ok()) {
        return material.error;
    }
    auto created = openssl_aead_packet_protector::create(output, level, std::move(material));
    return created.error;
}

[[nodiscard]] inline production_packet_protectors make_production_packet_protectors() {
    production_packet_protectors protectors{};
    if (auto error = install_protector(protectors.client_initial_tx, protection_level::initial, std::byte{0x11}); !error.ok()) {
        protectors.error = std::move(error);
        return protectors;
    }
    if (auto error = install_protector(protectors.server_initial_tx, protection_level::initial, std::byte{0x22}); !error.ok()) {
        protectors.error = std::move(error);
        return protectors;
    }
    if (auto error = install_protector(protectors.client_handshake_tx, protection_level::handshake, std::byte{0x33}); !error.ok()) {
        protectors.error = std::move(error);
        return protectors;
    }
    if (auto error = install_protector(protectors.server_handshake_tx, protection_level::handshake, std::byte{0x44}); !error.ok()) {
        protectors.error = std::move(error);
        return protectors;
    }
    if (auto error = install_protector(protectors.client_application_tx, protection_level::application, std::byte{0x55}); !error.ok()) {
        protectors.error = std::move(error);
        return protectors;
    }
    if (auto error = install_protector(protectors.server_application_tx, protection_level::application, std::byte{0x66}); !error.ok()) {
        protectors.error = std::move(error);
        return protectors;
    }
    return protectors;
}

inline void mark_application_ready(key_lifecycle_state& lifecycle) noexcept {
    lifecycle.observe_tls(handshake_state::handshake_confirmed, tls_key_availability{true, true, true});
}

} // namespace flowq::quic::test
