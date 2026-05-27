#pragma once

#include <flowq/buffer.hpp>
#include <flowq/quic/packet_header.hpp>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace flowq::quic {

class routing_table {
public:
    void add(const connection_id& cid, std::uint64_t connection_handle) {
        entries_[to_key(cid)] = connection_handle;
    }

    [[nodiscard]] std::optional<std::uint64_t> lookup(const connection_id& cid) const {
        auto it = entries_.find(to_key(cid));
        if (it == entries_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void retire(const connection_id& cid) {
        entries_.erase(to_key(cid));
    }

    [[nodiscard]] std::size_t active_count() const noexcept {
        return entries_.size();
    }

private:
    struct buffer_hash {
        std::size_t operator()(const std::vector<std::byte>& bytes) const noexcept {
            std::size_t hash = 0;
            for (auto byte : bytes) {
                hash ^= std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(byte)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
            return hash;
        }
    };

    std::unordered_map<std::vector<std::byte>, std::uint64_t, buffer_hash> entries_{};

    [[nodiscard]] static std::vector<std::byte> to_key(const connection_id& cid) {
        std::vector<std::byte> key;
        key.reserve(cid.bytes.size());
        for (std::size_t i = 0; i < cid.bytes.size(); ++i) {
            key.push_back(cid.bytes.data()[i]);
        }
        return key;
    }
};

[[nodiscard]] inline bool version_supported(std::uint32_t version, const std::vector<std::uint32_t>& supported) {
    for (const auto v : supported) {
        if (v == version) {
            return true;
        }
    }
    return false;
}

struct version_negotiation_result {
    flowq::buffer payload;
    connection_id destination_connection_id;
    connection_id source_connection_id;
    std::vector<std::uint32_t> supported_versions;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

[[nodiscard]] inline version_negotiation_result build_version_negotiation(
    connection_id destination,
    connection_id source,
    const std::vector<std::uint32_t>& supported_versions) {
    if (supported_versions.empty()) {
        return {{}, {}, {}, {}, flowq::error{flowq::error_code::protocol_error, "no supported versions provided"}};
    }

    std::vector<std::byte> payload;
    payload.push_back(std::byte{0x80});  // Header form + unused

    // Version = 0 (RFC 9000 §17.2.1)
    payload.push_back(std::byte{0x00});
    payload.push_back(std::byte{0x00});
    payload.push_back(std::byte{0x00});
    payload.push_back(std::byte{0x00});

    // DCID length + DCID
    payload.push_back(static_cast<std::byte>(destination.bytes.size()));
    for (std::size_t i = 0; i < destination.bytes.size(); ++i) {
        payload.push_back(destination.bytes.data()[i]);
    }
    // SCID length + SCID
    payload.push_back(static_cast<std::byte>(source.bytes.size()));
    for (std::size_t i = 0; i < source.bytes.size(); ++i) {
        payload.push_back(source.bytes.data()[i]);
    }
    // Supported versions
    for (const auto version : supported_versions) {
        payload.push_back(static_cast<std::byte>((version >> 24) & 0xff));
        payload.push_back(static_cast<std::byte>((version >> 16) & 0xff));
        payload.push_back(static_cast<std::byte>((version >> 8) & 0xff));
        payload.push_back(static_cast<std::byte>(version & 0xff));
    }

    return {flowq::buffer{payload}, destination, source, supported_versions, {}};
}

struct retry_token {
    flowq::buffer data;
};

class retry_token_validator {
public:
    [[nodiscard]] bool validate_token_shape(const retry_token& token) const noexcept {
        return !token.data.empty();
    }
};

struct retry_integrity_result {
    flowq::buffer tag;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

struct retry_verify_result {
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

class retry_integrity_provider {
public:
    [[nodiscard]] retry_integrity_result compute_integrity_tag(
        const flowq::buffer& retry_pseudo_header,
        const flowq::buffer& payload) {
        // Deterministic stub: XOR of input bytes as integrity tag
        std::vector<std::byte> tag;
        const auto max_size = std::max(retry_pseudo_header.size(), payload.size());
        tag.reserve(max_size);
        for (std::size_t i = 0; i < max_size; ++i) {
            auto a = i < retry_pseudo_header.size() ? retry_pseudo_header.data()[i] : std::byte{0};
            auto b = i < payload.size() ? payload.data()[i] : std::byte{0};
            tag.push_back(a ^ b);
        }
        return {flowq::buffer{tag}, {}};
    }

    [[nodiscard]] retry_verify_result verify_integrity_tag(
        const flowq::buffer& retry_pseudo_header,
        const flowq::buffer& tag) {
        // Deterministic stub: always succeeds if tag is non-empty
        if (tag.empty()) {
            return {flowq::error{flowq::error_code::protocol_error, "empty integrity tag"}};
        }
        return {{}};
    }
};

} // namespace flowq::quic
