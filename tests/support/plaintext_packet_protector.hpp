#pragma once

#include <flowq/quic/packet_pipeline.hpp>

#include <span>

namespace flowq::quic::test {

class plaintext_packet_protector final : public packet_protector {
public:
    explicit plaintext_packet_protector(protection_level level) noexcept : level_{level} {}

    [[nodiscard]] protection_level level() const noexcept override {
        return level_;
    }

    [[nodiscard]] packet_security_level security_level() const noexcept override {
        return packet_security_level::authenticated_encrypted;
    }

    [[nodiscard]] crypto_provider_status provider_status() const noexcept override {
        return crypto_provider_status::available(
            cipher_suite::aes_128_gcm_sha256,
            crypto_capabilities{
                true,
                true,
                true,
                true,
                false
            });
    }

    [[nodiscard]] packet_protection_result protect(std::span<const std::byte> plaintext) const override {
        return {flowq::buffer{plaintext}, {}};
    }

    [[nodiscard]] packet_protection_result unprotect(std::span<const std::byte> protected_payload) const override {
        return {flowq::buffer{protected_payload}, {}};
    }

private:
    protection_level level_;
};

struct plaintext_packet_protector_set {
    plaintext_packet_protector initial{protection_level::initial};
    plaintext_packet_protector handshake{protection_level::handshake};
    plaintext_packet_protector application{protection_level::application};
};

} // namespace flowq::quic::test
