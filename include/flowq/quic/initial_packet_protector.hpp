#pragma once

#include <flowq/error.hpp>
#include <flowq/quic/initial_keys.hpp>
#include <flowq/quic/packet_pipeline.hpp>

#include <cstddef>
#include <span>
#include <utility>

namespace flowq::quic {

enum class initial_packet_protection_direction {
    client,
    server
};

class initial_packet_protector final : public packet_protector {
public:
    [[nodiscard]] static initial_packet_protector client(const connection_id& destination_connection_id) {
        return client(std::span<const std::byte>{destination_connection_id.bytes.data(), destination_connection_id.bytes.size()});
    }

    [[nodiscard]] static initial_packet_protector server(const connection_id& destination_connection_id) {
        return server(std::span<const std::byte>{destination_connection_id.bytes.data(), destination_connection_id.bytes.size()});
    }

    [[nodiscard]] static initial_packet_protector client(std::span<const std::byte> destination_connection_id) {
        return initial_packet_protector{initial_packet_protection_direction::client, destination_connection_id};
    }

    [[nodiscard]] static initial_packet_protector server(std::span<const std::byte> destination_connection_id) {
        return initial_packet_protector{initial_packet_protection_direction::server, destination_connection_id};
    }

    [[nodiscard]] protection_level level() const noexcept override {
        return protection_level::initial;
    }

    [[nodiscard]] packet_security_level security_level() const noexcept override {
        return packet_security_level::authenticated_encrypted;
    }

    [[nodiscard]] crypto_provider_status provider_status() const noexcept override {
        return ready_ ? status_ : crypto_provider_status::unavailable();
    }

    [[nodiscard]] std::size_t protection_overhead() const noexcept override {
        return 16;
    }

    [[nodiscard]] bool header_protection_enabled() const noexcept override {
        return ready_;
    }

    [[nodiscard]] header_protection_mask_result header_protection_mask(std::span<const std::byte> sample) const override {
        if (!ready_) {
            return {{}, initialization_error_};
        }
        return initial_header_protection_mask(
            std::span<const std::byte>{header_protection_key_.data(), header_protection_key_.size()},
            sample);
    }

    [[nodiscard]] packet_protection_result protect(std::span<const std::byte> plaintext) const override {
        (void)plaintext;
        return {{}, flowq::error{flowq::error_code::protocol_error, "Initial packet protection requires packet context"}};
    }

    [[nodiscard]] packet_protection_result unprotect(std::span<const std::byte> protected_payload) const override {
        (void)protected_payload;
        return {{}, flowq::error{flowq::error_code::protocol_error, "Initial packet protection requires packet context"}};
    }

    [[nodiscard]] packet_protection_result protect(
        const packet_protection_context& context,
        std::span<const std::byte> plaintext) const override {
        if (auto error = validate_context(context); !error.ok()) {
            return {{}, error};
        }
        auto sealed = initial_aead_seal(
            std::span<const std::byte>{key_.data(), key_.size()},
            std::span<const std::byte>{iv_.data(), iv_.size()},
            context.number.value,
            context.associated_data,
            plaintext);
        return {std::move(sealed.payload), sealed.error};
    }

    [[nodiscard]] packet_protection_result unprotect(
        const packet_protection_context& context,
        std::span<const std::byte> protected_payload) const override {
        if (auto error = validate_context(context); !error.ok()) {
            return {{}, error};
        }
        auto opened = initial_aead_open(
            std::span<const std::byte>{key_.data(), key_.size()},
            std::span<const std::byte>{iv_.data(), iv_.size()},
            context.number.value,
            context.associated_data,
            protected_payload);
        return {std::move(opened.payload), opened.error};
    }

private:
    initial_packet_protector(
        initial_packet_protection_direction direction,
        std::span<const std::byte> destination_connection_id)
        : status_{initial_crypto_backend_status()},
          initialization_error_{initial_key_error("Initial packet protector is not initialized")} {
#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)
        auto secrets = derive_initial_secrets(destination_connection_id);
        if (!secrets.ok()) {
            initialization_error_ = secrets.error;
            return;
        }

        auto material = derive_initial_key_material(
            direction == initial_packet_protection_direction::client
                ? secrets.client_initial_secret
                : secrets.server_initial_secret);
        if (!material.ok()) {
            initialization_error_ = material.error;
            return;
        }

        key_ = std::move(material.key);
        iv_ = std::move(material.iv);
        header_protection_key_ = std::move(material.header_protection_key);
        initialization_error_ = {};
        ready_ = true;
#else
        (void)direction;
        (void)destination_connection_id;
        status_ = crypto_provider_status::unavailable();
        initialization_error_ = initial_key_error("OpenSSL crypto backend is disabled");
#endif
    }

    [[nodiscard]] flowq::error validate_context(const packet_protection_context& context) const {
        if (!ready_) {
            return initialization_error_;
        }
        if (context.number.space != packet_number_space::initial) {
            return flowq::error{flowq::error_code::protocol_error, "Initial packet protection requires Initial packet numbers"};
        }
        if (context.associated_data.empty()) {
            return flowq::error{flowq::error_code::protocol_error, "Initial packet protection requires associated data"};
        }
        return {};
    }

    crypto_provider_status status_{crypto_provider_status::unavailable()};
    flowq::buffer key_;
    flowq::buffer iv_;
    flowq::buffer header_protection_key_;
    flowq::error initialization_error_{initial_key_error("Initial packet protector is not initialized")};
    bool ready_{false};
};

} // namespace flowq::quic
