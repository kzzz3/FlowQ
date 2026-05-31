#pragma once

#include <flowq/quic/packet_pipeline.hpp>

#include <span>

namespace flowq::quic::test {

class plaintext_packet_protector final : public packet_protector {
public:
    [[nodiscard]] protection_level level() const noexcept override {
        return protection_level::none;
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
};

} // namespace flowq::quic::test
