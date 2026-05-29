#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>
#include <flowq/quic/crypto_provider.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)
#include <openssl/evp.h>
#include <openssl/kdf.h>
#endif

namespace flowq::quic {

struct initial_secrets_result {
    flowq::buffer initial_secret;
    flowq::buffer client_initial_secret;
    flowq::buffer server_initial_secret;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

struct initial_key_material_result {
    flowq::buffer key;
    flowq::buffer iv;
    flowq::buffer header_protection_key;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

[[nodiscard]] inline flowq::error initial_key_error(const char* message) {
    return flowq::error{flowq::error_code::tls_error, message};
}

[[nodiscard]] inline flowq::buffer rfc9001_initial_salt() {
    constexpr std::array salt{
        std::byte{0x38}, std::byte{0x76}, std::byte{0x2c}, std::byte{0xf7}, std::byte{0xf5},
        std::byte{0x59}, std::byte{0x34}, std::byte{0xb3}, std::byte{0x4d}, std::byte{0x17},
        std::byte{0x9a}, std::byte{0xe6}, std::byte{0xa4}, std::byte{0xc8}, std::byte{0x0c},
        std::byte{0xad}, std::byte{0xcc}, std::byte{0xbb}, std::byte{0x7f}, std::byte{0x0a}
    };
    return flowq::buffer{salt};
}

[[nodiscard]] inline crypto_provider_status initial_crypto_backend_status() noexcept {
#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)
    return crypto_provider_status::available(
        cipher_suite::aes_128_gcm_sha256,
        crypto_capabilities{true, true, true, true, false});
#else
    return crypto_provider_status::unavailable();
#endif
}

#ifndef FLOWQ_HIDE_DETAIL
namespace detail {

[[nodiscard]] inline std::vector<std::byte> tls13_hkdf_label(std::size_t length, std::string_view label) {
    constexpr std::string_view prefix = "tls13 ";
    std::vector<std::byte> info;
    info.reserve(2 + 1 + prefix.size() + label.size() + 1);
    info.push_back(static_cast<std::byte>((length >> 8U) & 0xffU));
    info.push_back(static_cast<std::byte>(length & 0xffU));
    info.push_back(static_cast<std::byte>(prefix.size() + label.size()));
    for (auto character : prefix) {
        info.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    for (auto character : label) {
        info.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    info.push_back(std::byte{0x00});
    return info;
}

[[nodiscard]] inline flowq::buffer make_buffer(const std::vector<std::byte>& bytes) {
    return flowq::buffer{bytes};
}

#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)

struct evp_pkey_context {
    EVP_PKEY_CTX* value{};

    explicit evp_pkey_context(int id) : value{EVP_PKEY_CTX_new_id(id, nullptr)} {}

    evp_pkey_context(const evp_pkey_context&) = delete;
    evp_pkey_context& operator=(const evp_pkey_context&) = delete;

    evp_pkey_context(evp_pkey_context&& other) noexcept : value{other.value} {
        other.value = nullptr;
    }

    evp_pkey_context& operator=(evp_pkey_context&& other) noexcept {
        if (this != &other) {
            EVP_PKEY_CTX_free(value);
            value = other.value;
            other.value = nullptr;
        }
        return *this;
    }

    ~evp_pkey_context() {
        EVP_PKEY_CTX_free(value);
    }
};

struct evp_cipher_context {
    EVP_CIPHER_CTX* value{EVP_CIPHER_CTX_new()};

    evp_cipher_context() = default;

    evp_cipher_context(const evp_cipher_context&) = delete;
    evp_cipher_context& operator=(const evp_cipher_context&) = delete;

    evp_cipher_context(evp_cipher_context&& other) noexcept : value{other.value} {
        other.value = nullptr;
    }

    evp_cipher_context& operator=(evp_cipher_context&& other) noexcept {
        if (this != &other) {
            EVP_CIPHER_CTX_free(value);
            value = other.value;
            other.value = nullptr;
        }
        return *this;
    }

    ~evp_cipher_context() {
        EVP_CIPHER_CTX_free(value);
    }
};

[[nodiscard]] inline crypto_bytes_result hkdf_extract_sha256(std::span<const std::byte> salt, std::span<const std::byte> key) {
    evp_pkey_context context{EVP_PKEY_HKDF};
    if (context.value == nullptr || EVP_PKEY_derive_init(context.value) <= 0) {
        return {{}, initial_key_error("OpenSSL HKDF context initialization failed")};
    }
    if (EVP_PKEY_CTX_hkdf_mode(context.value, EVP_PKEY_HKDEF_MODE_EXTRACT_ONLY) <= 0 ||
        EVP_PKEY_CTX_set_hkdf_md(context.value, EVP_sha256()) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_salt(context.value, reinterpret_cast<const unsigned char*>(salt.data()), static_cast<int>(salt.size())) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_key(context.value, reinterpret_cast<const unsigned char*>(key.data()), static_cast<int>(key.size())) <= 0) {
        return {{}, initial_key_error("OpenSSL HKDF extract setup failed")};
    }

    std::vector<std::byte> output(32);
    auto output_size = output.size();
    if (EVP_PKEY_derive(context.value, reinterpret_cast<unsigned char*>(output.data()), &output_size) <= 0 || output_size != output.size()) {
        return {{}, initial_key_error("OpenSSL HKDF extract failed")};
    }
    return {make_buffer(output), {}};
}

[[nodiscard]] inline crypto_bytes_result hkdf_expand_label_sha256(std::span<const std::byte> secret, std::string_view label, std::size_t length) {
    auto info = tls13_hkdf_label(length, label);
    evp_pkey_context context{EVP_PKEY_HKDF};
    if (context.value == nullptr || EVP_PKEY_derive_init(context.value) <= 0) {
        return {{}, initial_key_error("OpenSSL HKDF context initialization failed")};
    }
    if (EVP_PKEY_CTX_hkdf_mode(context.value, EVP_PKEY_HKDEF_MODE_EXPAND_ONLY) <= 0 ||
        EVP_PKEY_CTX_set_hkdf_md(context.value, EVP_sha256()) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_key(context.value, reinterpret_cast<const unsigned char*>(secret.data()), static_cast<int>(secret.size())) <= 0 ||
        EVP_PKEY_CTX_add1_hkdf_info(context.value, reinterpret_cast<const unsigned char*>(info.data()), static_cast<int>(info.size())) <= 0) {
        return {{}, initial_key_error("OpenSSL HKDF expand setup failed")};
    }

    std::vector<std::byte> output(length);
    auto output_size = output.size();
    if (EVP_PKEY_derive(context.value, reinterpret_cast<unsigned char*>(output.data()), &output_size) <= 0 || output_size != output.size()) {
        return {{}, initial_key_error("OpenSSL HKDF expand failed")};
    }
    return {make_buffer(output), {}};
}

[[nodiscard]] inline std::vector<std::byte> nonce_for_packet_number(std::span<const std::byte> iv, std::uint64_t packet_number) {
    std::vector<std::byte> nonce{iv.begin(), iv.end()};
    for (std::size_t index = 0; index < 8; ++index) {
        nonce[nonce.size() - 1U - index] ^= static_cast<std::byte>((packet_number >> (index * 8U)) & 0xffU);
    }
    return nonce;
}

#endif

} // namespace detail
#endif // FLOWQ_HIDE_DETAIL

[[nodiscard]] inline initial_secrets_result derive_initial_secrets(std::span<const std::byte> destination_connection_id) {
#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)
    auto salt = rfc9001_initial_salt();
    auto initial = detail::hkdf_extract_sha256(std::span<const std::byte>{salt.data(), salt.size()}, destination_connection_id);
    if (!initial.ok()) {
        return {{}, {}, {}, initial.error};
    }
    auto client = detail::hkdf_expand_label_sha256(std::span<const std::byte>{initial.payload.data(), initial.payload.size()}, "client in", 32);
    if (!client.ok()) {
        return {{}, {}, {}, client.error};
    }
    auto server = detail::hkdf_expand_label_sha256(std::span<const std::byte>{initial.payload.data(), initial.payload.size()}, "server in", 32);
    if (!server.ok()) {
        return {{}, {}, {}, server.error};
    }
    return {std::move(initial.payload), std::move(client.payload), std::move(server.payload), {}};
#else
    (void)destination_connection_id;
    return {{}, {}, {}, initial_key_error("OpenSSL crypto backend is disabled")};
#endif
}

[[nodiscard]] inline initial_key_material_result derive_initial_key_material(std::span<const std::byte> initial_secret) {
#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)
    auto key = detail::hkdf_expand_label_sha256(initial_secret, "quic key", 16);
    if (!key.ok()) {
        return {{}, {}, {}, key.error};
    }
    auto iv = detail::hkdf_expand_label_sha256(initial_secret, "quic iv", 12);
    if (!iv.ok()) {
        return {{}, {}, {}, iv.error};
    }
    auto hp = detail::hkdf_expand_label_sha256(initial_secret, "quic hp", 16);
    if (!hp.ok()) {
        return {{}, {}, {}, hp.error};
    }
    return {std::move(key.payload), std::move(iv.payload), std::move(hp.payload), {}};
#else
    (void)initial_secret;
    return {{}, {}, {}, initial_key_error("OpenSSL crypto backend is disabled")};
#endif
}

[[nodiscard]] inline initial_key_material_result derive_initial_key_material(const flowq::buffer& initial_secret) {
    return derive_initial_key_material(std::span<const std::byte>{initial_secret.data(), initial_secret.size()});
}

[[nodiscard]] inline header_protection_mask_result initial_header_protection_mask(
    std::span<const std::byte> header_protection_key,
    std::span<const std::byte> sample) {
#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)
    if (header_protection_key.size() != 16 || sample.size() != 16) {
        return {{}, initial_key_error("AES-128 header protection requires 16-byte key and sample")};
    }
    detail::evp_cipher_context context{};
    if (context.value == nullptr ||
        EVP_EncryptInit_ex(context.value, EVP_aes_128_ecb(), nullptr, reinterpret_cast<const unsigned char*>(header_protection_key.data()), nullptr) <= 0 ||
        EVP_CIPHER_CTX_set_padding(context.value, 0) <= 0) {
        return {{}, initial_key_error("OpenSSL AES-ECB header protection setup failed")};
    }
    std::array<std::byte, 16> encrypted{};
    int written = 0;
    if (EVP_EncryptUpdate(context.value, reinterpret_cast<unsigned char*>(encrypted.data()), &written, reinterpret_cast<const unsigned char*>(sample.data()), static_cast<int>(sample.size())) <= 0 || written != 16) {
        return {{}, initial_key_error("OpenSSL AES-ECB header protection failed")};
    }
    int final_written = 0;
    if (EVP_EncryptFinal_ex(context.value, reinterpret_cast<unsigned char*>(encrypted.data()) + written, &final_written) <= 0 || final_written != 0) {
        return {{}, initial_key_error("OpenSSL AES-ECB header protection finalization failed")};
    }
    return {{encrypted[0], encrypted[1], encrypted[2], encrypted[3], encrypted[4]}, {}};
#else
    (void)header_protection_key;
    (void)sample;
    return {{}, initial_key_error("OpenSSL crypto backend is disabled")};
#endif
}

[[nodiscard]] inline crypto_bytes_result initial_aead_seal(
    std::span<const std::byte> key,
    std::span<const std::byte> iv,
    std::uint64_t packet_number,
    std::span<const std::byte> aad,
    std::span<const std::byte> plaintext) {
#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)
    if (key.size() != 16 || iv.size() != 12) {
        return {{}, initial_key_error("AES-128-GCM Initial protection requires 16-byte key and 12-byte IV")};
    }
    auto nonce = detail::nonce_for_packet_number(iv, packet_number);
    detail::evp_cipher_context context{};
    if (context.value == nullptr ||
        EVP_EncryptInit_ex(context.value, EVP_aes_128_gcm(), nullptr, nullptr, nullptr) <= 0 ||
        EVP_CIPHER_CTX_ctrl(context.value, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) <= 0 ||
        EVP_EncryptInit_ex(context.value, nullptr, nullptr, reinterpret_cast<const unsigned char*>(key.data()), reinterpret_cast<const unsigned char*>(nonce.data())) <= 0) {
        return {{}, initial_key_error("OpenSSL AES-GCM seal setup failed")};
    }
    int written = 0;
    if (!aad.empty() && EVP_EncryptUpdate(context.value, nullptr, &written, reinterpret_cast<const unsigned char*>(aad.data()), static_cast<int>(aad.size())) <= 0) {
        return {{}, initial_key_error("OpenSSL AES-GCM AAD setup failed")};
    }
    std::vector<std::byte> output(plaintext.size() + 16);
    if (!plaintext.empty() && EVP_EncryptUpdate(context.value, reinterpret_cast<unsigned char*>(output.data()), &written, reinterpret_cast<const unsigned char*>(plaintext.data()), static_cast<int>(plaintext.size())) <= 0) {
        return {{}, initial_key_error("OpenSSL AES-GCM seal failed")};
    }
    int total_written = written;
    int final_written = 0;
    if (EVP_EncryptFinal_ex(context.value, reinterpret_cast<unsigned char*>(output.data()) + total_written, &final_written) <= 0) {
        return {{}, initial_key_error("OpenSSL AES-GCM seal finalization failed")};
    }
    total_written += final_written;
    if (EVP_CIPHER_CTX_ctrl(context.value, EVP_CTRL_GCM_GET_TAG, 16, reinterpret_cast<unsigned char*>(output.data()) + total_written) <= 0) {
        return {{}, initial_key_error("OpenSSL AES-GCM tag extraction failed")};
    }
    output.resize(static_cast<std::size_t>(total_written) + 16U);
    return {detail::make_buffer(output), {}};
#else
    (void)key;
    (void)iv;
    (void)packet_number;
    (void)aad;
    (void)plaintext;
    return {{}, initial_key_error("OpenSSL crypto backend is disabled")};
#endif
}

[[nodiscard]] inline crypto_bytes_result initial_aead_open(
    std::span<const std::byte> key,
    std::span<const std::byte> iv,
    std::uint64_t packet_number,
    std::span<const std::byte> aad,
    std::span<const std::byte> protected_payload) {
#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)
    if (key.size() != 16 || iv.size() != 12 || protected_payload.size() < 16) {
        return {{}, initial_key_error("AES-128-GCM Initial open requires key, IV, and authentication tag")};
    }
    const auto ciphertext_size = protected_payload.size() - 16U;
    auto nonce = detail::nonce_for_packet_number(iv, packet_number);
    detail::evp_cipher_context context{};
    if (context.value == nullptr ||
        EVP_DecryptInit_ex(context.value, EVP_aes_128_gcm(), nullptr, nullptr, nullptr) <= 0 ||
        EVP_CIPHER_CTX_ctrl(context.value, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) <= 0 ||
        EVP_DecryptInit_ex(context.value, nullptr, nullptr, reinterpret_cast<const unsigned char*>(key.data()), reinterpret_cast<const unsigned char*>(nonce.data())) <= 0) {
        return {{}, initial_key_error("OpenSSL AES-GCM open setup failed")};
    }
    int written = 0;
    if (!aad.empty() && EVP_DecryptUpdate(context.value, nullptr, &written, reinterpret_cast<const unsigned char*>(aad.data()), static_cast<int>(aad.size())) <= 0) {
        return {{}, initial_key_error("OpenSSL AES-GCM open AAD setup failed")};
    }
    std::vector<std::byte> output(ciphertext_size);
    if (ciphertext_size > 0 && EVP_DecryptUpdate(context.value, reinterpret_cast<unsigned char*>(output.data()), &written, reinterpret_cast<const unsigned char*>(protected_payload.data()), static_cast<int>(ciphertext_size)) <= 0) {
        return {{}, initial_key_error("OpenSSL AES-GCM open failed")};
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): OpenSSL EVP_CTRL_GCM_SET_TAG takes void* non-const
    // but only reads the tag bytes. This const_cast is required by the OpenSSL API deficiency.
    if (EVP_CIPHER_CTX_ctrl(context.value, EVP_CTRL_GCM_SET_TAG, 16, const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(protected_payload.data() + static_cast<std::ptrdiff_t>(ciphertext_size)))) <= 0) {
        return {{}, initial_key_error("OpenSSL AES-GCM tag setup failed")};
    }
    int final_written = 0;
    if (EVP_DecryptFinal_ex(context.value, reinterpret_cast<unsigned char*>(output.data()) + written, &final_written) <= 0) {
        return {{}, initial_key_error("OpenSSL AES-GCM authentication failed")};
    }
    output.resize(static_cast<std::size_t>(written + final_written));
    return {detail::make_buffer(output), {}};
#else
    (void)key;
    (void)iv;
    (void)packet_number;
    (void)aad;
    (void)protected_payload;
    return {{}, initial_key_error("OpenSSL crypto backend is disabled")};
#endif
}

[[nodiscard]] inline crypto_bytes_result initial_aead_open(
    std::span<const std::byte> key,
    std::span<const std::byte> iv,
    std::uint64_t packet_number,
    std::span<const std::byte> aad,
    const flowq::buffer& protected_payload) {
    return initial_aead_open(key, iv, packet_number, aad, std::span<const std::byte>{protected_payload.data(), protected_payload.size()});
}

} // namespace flowq::quic
