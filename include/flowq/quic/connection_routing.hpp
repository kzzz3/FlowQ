#pragma once

#include <flowq/buffer.hpp>
#include <flowq/endpoint.hpp>
#include <flowq/quic/packet_header.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)
#include <openssl/rand.h>
#endif

namespace flowq::quic {

inline constexpr std::size_t stateless_reset_token_size = 16;
inline constexpr std::size_t minimum_stateless_reset_datagram_size = 21;
inline constexpr std::size_t default_stateless_reset_datagram_size = 41;

using stateless_reset_token = std::array<std::byte, stateless_reset_token_size>;

struct random_bytes_result {
    flowq::buffer bytes;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

using random_bytes_generator = std::function<random_bytes_result(std::size_t)>;

struct stateless_reset_packet_config {
    std::size_t preferred_datagram_size{default_stateless_reset_datagram_size};
    random_bytes_generator random{};
};

struct stateless_reset_packet_result {
    flowq::buffer payload;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

namespace detail {

struct buffer_hash {
    std::size_t operator()(const std::vector<std::byte>& bytes) const noexcept {
        std::size_t hash = 0;
        for (auto byte : bytes) {
            hash ^= std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(byte)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return hash;
    }
};

[[nodiscard]] inline std::vector<std::byte> connection_id_key(const connection_id& cid) {
    std::vector<std::byte> key;
    key.reserve(cid.bytes.size());
    for (std::size_t i = 0; i < cid.bytes.size(); ++i) {
        key.push_back(cid.bytes.data()[i]);
    }
    return key;
}

[[nodiscard]] inline flowq::error stateless_reset_error(std::string message) {
    return flowq::error{flowq::error_code::protocol_error, std::move(message)};
}

[[nodiscard]] inline random_bytes_result secure_random_bytes(std::size_t size) {
#if defined(FLOWQ_ENABLE_OPENSSL_CRYPTO)
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {{}, flowq::error{flowq::error_code::internal_error, "secure random request is too large"}};
    }
    std::vector<std::byte> bytes(size);
    if (size == 0) {
        return {flowq::buffer{std::move(bytes)}, {}};
    }
    if (RAND_bytes(reinterpret_cast<unsigned char*>(bytes.data()), static_cast<int>(bytes.size())) != 1) {
        return {{}, flowq::error{flowq::error_code::internal_error, "openssl random generation failed"}};
    }
    return {flowq::buffer{std::move(bytes)}, {}};
#else
    (void)size;
    return {{}, flowq::error{flowq::error_code::internal_error, "secure random provider is not configured"}};
#endif
}

} // namespace detail

[[nodiscard]] inline stateless_reset_packet_result build_stateless_reset_packet(
    const stateless_reset_token& token,
    std::size_t triggering_datagram_size,
    const stateless_reset_packet_config& config = {}) {
    if (triggering_datagram_size <= minimum_stateless_reset_datagram_size) {
        return {{}, detail::stateless_reset_error("triggering datagram is too small for stateless reset")};
    }
    if (config.preferred_datagram_size < minimum_stateless_reset_datagram_size) {
        return {{}, detail::stateless_reset_error("stateless reset datagram size is too small")};
    }

    const auto reset_size = std::min(config.preferred_datagram_size, triggering_datagram_size - 1);
    if (reset_size < minimum_stateless_reset_datagram_size) {
        return {{}, detail::stateless_reset_error("stateless reset cannot be smaller than triggering datagram")};
    }

    const auto random_size = reset_size - stateless_reset_token_size;
    const auto random = config.random ? config.random(random_size) : detail::secure_random_bytes(random_size);
    if (!random.ok()) {
        return {{}, random.error};
    }
    if (random.bytes.size() != random_size) {
        return {{}, flowq::error{flowq::error_code::internal_error, "stateless reset random provider returned wrong size"}};
    }

    std::vector<std::byte> payload;
    payload.reserve(reset_size);
    for (std::size_t i = 0; i < random.bytes.size(); ++i) {
        payload.push_back(random.bytes.data()[i]);
    }
    payload[0] = static_cast<std::byte>((static_cast<std::uint8_t>(payload[0]) & 0x3FU) | 0x40U);
    payload.insert(payload.end(), token.begin(), token.end());
    return {flowq::buffer{std::move(payload)}, {}};
}

class routing_table {
public:
    void add(const connection_id& cid, std::uint64_t connection_handle) {
        entries_[detail::connection_id_key(cid)] = connection_handle;
    }

    void add_stateless_reset_token(const connection_id& cid, stateless_reset_token token) {
        reset_tokens_[detail::connection_id_key(cid)] = token;
    }

    /// Look up a connection handle by destination connection ID.
    /// Returns nullopt if the CID is not registered.
    [[nodiscard]] std::optional<std::uint64_t> lookup(const connection_id& cid) const {
        auto it = entries_.find(detail::connection_id_key(cid));
        if (it == entries_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    [[nodiscard]] std::optional<stateless_reset_token> lookup_stateless_reset_token(const connection_id& cid) const {
        auto it = reset_tokens_.find(detail::connection_id_key(cid));
        if (it == reset_tokens_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    /// Retire a connection ID, removing it from the routing table.
    void retire(const connection_id& cid) {
        entries_.erase(detail::connection_id_key(cid));
    }

    /// Return the number of active connection ID mappings.
    [[nodiscard]] std::size_t active_count() const noexcept {
        return entries_.size();
    }

private:
    std::unordered_map<std::vector<std::byte>, std::uint64_t, detail::buffer_hash> entries_{};
    std::unordered_map<std::vector<std::byte>, stateless_reset_token, detail::buffer_hash> reset_tokens_{};
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

namespace detail {

/// Compare tag bytes without early exit on mismatch or length differences.
/// This avoids leaking which byte or length check failed through branch timing.
[[nodiscard]] inline bool constant_time_equal(std::span<const std::byte> expected, std::span<const std::byte> candidate) noexcept {
    const auto max_size = std::max(expected.size(), candidate.size());
    auto difference = static_cast<std::uint8_t>(expected.size() ^ candidate.size());

    for (std::size_t i = 0; i < max_size; ++i) {
        const auto expected_byte = i < expected.size() ? static_cast<std::uint8_t>(expected[i]) : 0U;
        const auto candidate_byte = i < candidate.size() ? static_cast<std::uint8_t>(candidate[i]) : 0U;
        difference = static_cast<std::uint8_t>(difference | (expected_byte ^ candidate_byte));
    }

    return difference == 0;
}

[[nodiscard]] inline flowq::error retry_token_error(std::string message) {
    return flowq::error{flowq::error_code::protocol_error, std::move(message)};
}

inline void append_u16(std::vector<std::byte>& output, std::uint16_t value) {
    output.push_back(static_cast<std::byte>((value >> 8) & 0xffU));
    output.push_back(static_cast<std::byte>(value & 0xffU));
}

inline void append_u64(std::vector<std::byte>& output, std::uint64_t value) {
    for (auto shift = 56; shift >= 0; shift -= 8) {
        output.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
    }
}

inline void append_bytes(std::vector<std::byte>& output, std::span<const std::byte> bytes) {
    output.insert(output.end(), bytes.begin(), bytes.end());
}

inline void append_string(std::vector<std::byte>& output, const std::string& value) {
    for (const auto character : value) {
        output.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
}

[[nodiscard]] inline bool read_u16(std::span<const std::byte> input, std::size_t& offset, std::uint16_t& value) noexcept {
    if (offset + 2 > input.size()) {
        return false;
    }
    value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(input[offset]) << 8) | static_cast<std::uint16_t>(input[offset + 1]));
    offset += 2;
    return true;
}

[[nodiscard]] inline bool read_u64(std::span<const std::byte> input, std::size_t& offset, std::uint64_t& value) noexcept {
    if (offset + 8 > input.size()) {
        return false;
    }
    value = 0;
    for (auto index = 0U; index < 8U; ++index) {
        value = (value << 8) | static_cast<std::uint64_t>(input[offset + index]);
    }
    offset += 8;
    return true;
}

[[nodiscard]] inline bool read_bytes(std::span<const std::byte> input, std::size_t& offset, std::uint16_t size, std::span<const std::byte>& bytes) noexcept {
    if (offset + size > input.size()) {
        return false;
    }
    bytes = std::span<const std::byte>{input.data() + offset, size};
    offset += size;
    return true;
}

[[nodiscard]] inline flowq::buffer retry_token_integrity_context() {
    std::vector<std::byte> context;
    context.reserve(18);
    append_string(context, "flowq.retry.v1");
    return flowq::buffer{std::move(context)};
}

} // namespace detail

/// Result of Retry integrity tag creation.
struct retry_integrity_result {
    flowq::buffer tag;
    flowq::error error{};

    /// Return whether tag creation completed without an error.
    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

/// Result of Retry integrity tag verification.
struct retry_verify_result {
    flowq::error error{};

    /// Return whether tag verification completed without an error.
    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

/// Provider boundary used to create and verify Retry integrity tags.
class retry_integrity_provider {
public:
    using tag_function = std::function<retry_integrity_result(const flowq::buffer&, const flowq::buffer&)>;

    retry_integrity_provider() = default;

    explicit retry_integrity_provider(tag_function compute_tag)
        : compute_tag_{std::move(compute_tag)} {}

    [[nodiscard]] retry_integrity_result compute_integrity_tag(
        const flowq::buffer& retry_pseudo_header,
        const flowq::buffer& payload) const {
        if (!compute_tag_) {
            return {{}, flowq::error{flowq::error_code::protocol_error, "retry integrity provider is not configured"}};
        }
        return compute_tag_(retry_pseudo_header, payload);
    }

    [[nodiscard]] retry_verify_result verify_integrity_tag(
        const flowq::buffer& retry_pseudo_header,
        const flowq::buffer& payload,
        const flowq::buffer& tag) const {
        if (tag.empty()) {
            return {flowq::error{flowq::error_code::protocol_error, "empty integrity tag"}};
        }

        const auto expected = compute_integrity_tag(retry_pseudo_header, payload);
        if (!expected.ok()) {
            return {expected.error};
        }

        if (!detail::constant_time_equal(
                std::span<const std::byte>{expected.tag.data(), expected.tag.size()},
                std::span<const std::byte>{tag.data(), tag.size()})) {
            return {flowq::error{flowq::error_code::protocol_error, "retry integrity tag mismatch"}};
        }

        return {{}};
    }

private:
    tag_function compute_tag_{};
};

/// Configuration for address-validation Retry token lifetime.
struct retry_token_validator_config {
    std::chrono::system_clock::duration lifetime{std::chrono::minutes{5}};
};

/// Inputs that bind an issued Retry token to one client address and original DCID.
struct retry_token_context {
    flowq::endpoint remote;
    connection_id original_destination_connection_id;
    std::chrono::system_clock::time_point now{std::chrono::system_clock::now()};
};

/// Result of issuing an address-validation Retry token.
struct retry_token_issue_result {
    retry_token token;
    flowq::error error{};

    /// Return whether token issuance completed without an error.
    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

/// Result of validating an address-validation Retry token.
struct retry_token_validation_result {
    flowq::error error{};

    /// Return whether token validation completed without an error.
    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

namespace detail {

struct retry_token_payload_encode_result {
    flowq::buffer payload;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

struct retry_token_parts_result {
    flowq::buffer payload;
    flowq::buffer tag;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

struct decoded_retry_token_payload {
    std::uint64_t issued_at_seconds{};
    std::string host;
    std::uint16_t port{};
    connection_id original_destination_connection_id;
};

struct retry_token_payload_decode_result {
    decoded_retry_token_payload payload;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

[[nodiscard]] inline flowq::error validate_retry_token_lifetime(std::chrono::system_clock::duration lifetime) {
    if (lifetime <= std::chrono::system_clock::duration::zero()) {
        return retry_token_error("retry token lifetime must be positive");
    }
    return {};
}

[[nodiscard]] inline retry_token_payload_encode_result encode_retry_token_payload(const retry_token_context& context) {
    const auto issued_at_seconds = std::chrono::duration_cast<std::chrono::seconds>(context.now.time_since_epoch()).count();
    if (issued_at_seconds < 0) {
        return {{}, retry_token_error("retry token issue time is before epoch")};
    }
    if (context.remote.host.size() > std::numeric_limits<std::uint16_t>::max()) {
        return {{}, retry_token_error("retry token host is too large")};
    }
    if (context.original_destination_connection_id.bytes.size() > std::numeric_limits<std::uint16_t>::max()) {
        return {{}, retry_token_error("retry token connection ID is too large")};
    }

    const auto cid_size = context.original_destination_connection_id.bytes.size();
    std::vector<std::byte> payload;
    payload.reserve(8 + 2 + context.remote.host.size() + 2 + 2 + cid_size);
    append_u64(payload, static_cast<std::uint64_t>(issued_at_seconds));
    append_u16(payload, static_cast<std::uint16_t>(context.remote.host.size()));
    append_string(payload, context.remote.host);
    append_u16(payload, context.remote.port);
    append_u16(payload, static_cast<std::uint16_t>(cid_size));
    append_bytes(payload, std::span<const std::byte>{context.original_destination_connection_id.bytes.data(), cid_size});
    return {flowq::buffer{std::move(payload)}, {}};
}

[[nodiscard]] inline retry_token_issue_result encode_retry_token(const flowq::buffer& payload, const flowq::buffer& tag) {
    if (payload.size() > std::numeric_limits<std::uint16_t>::max()) {
        return {{}, retry_token_error("retry token payload is too large")};
    }
    if (tag.size() > std::numeric_limits<std::uint16_t>::max()) {
        return {{}, retry_token_error("retry token tag is too large")};
    }

    std::vector<std::byte> token;
    token.reserve(7 + payload.size() + 2 + tag.size());
    append_string(token, "FQRT");
    token.push_back(std::byte{0x01});
    append_u16(token, static_cast<std::uint16_t>(payload.size()));
    append_bytes(token, std::span<const std::byte>{payload.data(), payload.size()});
    append_u16(token, static_cast<std::uint16_t>(tag.size()));
    append_bytes(token, std::span<const std::byte>{tag.data(), tag.size()});
    return {retry_token{flowq::buffer{std::move(token)}}, {}};
}

[[nodiscard]] inline retry_token_parts_result parse_retry_token(const retry_token& token) {
    auto input = std::span<const std::byte>{token.data.data(), token.data.size()};
    if (input.size() < 9) {
        return {{}, {}, retry_token_error("retry token is too short")};
    }
    if (input[0] != std::byte{'F'} || input[1] != std::byte{'Q'} || input[2] != std::byte{'R'} || input[3] != std::byte{'T'}) {
        return {{}, {}, retry_token_error("retry token magic mismatch")};
    }
    if (input[4] != std::byte{0x01}) {
        return {{}, {}, retry_token_error("unsupported retry token version")};
    }

    std::size_t offset = 5;
    std::uint16_t payload_size = 0;
    if (!read_u16(input, offset, payload_size)) {
        return {{}, {}, retry_token_error("retry token payload length is truncated")};
    }

    std::span<const std::byte> payload_bytes;
    if (!read_bytes(input, offset, payload_size, payload_bytes)) {
        return {{}, {}, retry_token_error("retry token payload is truncated")};
    }

    std::uint16_t tag_size = 0;
    if (!read_u16(input, offset, tag_size)) {
        return {{}, {}, retry_token_error("retry token tag length is truncated")};
    }
    if (tag_size == 0) {
        return {{}, {}, retry_token_error("retry token tag is empty")};
    }

    std::span<const std::byte> tag_bytes;
    if (!read_bytes(input, offset, tag_size, tag_bytes) || offset != input.size()) {
        return {{}, {}, retry_token_error("retry token tag is truncated")};
    }

    return {flowq::buffer{payload_bytes}, flowq::buffer{tag_bytes}, {}};
}

[[nodiscard]] inline retry_token_payload_decode_result decode_retry_token_payload(const flowq::buffer& payload) {
    auto input = std::span<const std::byte>{payload.data(), payload.size()};
    std::size_t offset = 0;
    decoded_retry_token_payload decoded{};

    if (!read_u64(input, offset, decoded.issued_at_seconds)) {
        return {{}, retry_token_error("retry token issue time is truncated")};
    }

    std::uint16_t host_size = 0;
    if (!read_u16(input, offset, host_size)) {
        return {{}, retry_token_error("retry token host length is truncated")};
    }

    std::span<const std::byte> host_bytes;
    if (!read_bytes(input, offset, host_size, host_bytes)) {
        return {{}, retry_token_error("retry token host is truncated")};
    }
    decoded.host.reserve(host_bytes.size());
    for (auto byte : host_bytes) {
        decoded.host.push_back(static_cast<char>(static_cast<unsigned char>(byte)));
    }

    if (!read_u16(input, offset, decoded.port)) {
        return {{}, retry_token_error("retry token port is truncated")};
    }

    std::uint16_t cid_size = 0;
    if (!read_u16(input, offset, cid_size)) {
        return {{}, retry_token_error("retry token connection ID length is truncated")};
    }

    std::span<const std::byte> cid_bytes;
    if (!read_bytes(input, offset, cid_size, cid_bytes) || offset != input.size()) {
        return {{}, retry_token_error("retry token connection ID is truncated")};
    }
    decoded.original_destination_connection_id = connection_id{flowq::buffer{cid_bytes}};

    return {std::move(decoded), {}};
}

} // namespace detail

/// Issues and validates address-bound Retry tokens.
class retry_token_validator {
public:
    retry_token_validator() = default;

    explicit retry_token_validator(
        retry_integrity_provider integrity_provider,
        retry_token_validator_config config = {})
        : integrity_provider_{std::move(integrity_provider)}, config_{config} {}

    /// Return whether the opaque token has the minimal non-empty shape.
    [[nodiscard]] bool validate_token_shape(const retry_token& token) const noexcept {
        return !token.data.empty();
    }

    /// Issue a token bound to the peer address and original destination connection ID.
    [[nodiscard]] retry_token_issue_result issue_token(const retry_token_context& context) const {
        if (const auto error = detail::validate_retry_token_lifetime(config_.lifetime); !error.ok()) {
            return {{}, error};
        }

        const auto payload = detail::encode_retry_token_payload(context);
        if (!payload.ok()) {
            return {{}, payload.error};
        }

        const auto tag = integrity_provider_.compute_integrity_tag(detail::retry_token_integrity_context(), payload.payload);
        if (!tag.ok()) {
            return {{}, tag.error};
        }

        return detail::encode_retry_token(payload.payload, tag.tag);
    }

    /// Validate a token against the peer address, original destination connection ID, and configured lifetime.
    [[nodiscard]] retry_token_validation_result validate_token(const retry_token& token, const retry_token_context& context) const {
        if (!validate_token_shape(token)) {
            return {detail::retry_token_error("empty retry token")};
        }
        if (const auto error = detail::validate_retry_token_lifetime(config_.lifetime); !error.ok()) {
            return {error};
        }

        const auto parts = detail::parse_retry_token(token);
        if (!parts.ok()) {
            return {parts.error};
        }

        const auto verified = integrity_provider_.verify_integrity_tag(detail::retry_token_integrity_context(), parts.payload, parts.tag);
        if (!verified.ok()) {
            return {verified.error};
        }

        const auto decoded = detail::decode_retry_token_payload(parts.payload);
        if (!decoded.ok()) {
            return {decoded.error};
        }
        if (decoded.payload.issued_at_seconds > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return {detail::retry_token_error("retry token issue time is out of range")};
        }

        const auto issued_at = std::chrono::system_clock::time_point{
            std::chrono::seconds{static_cast<std::int64_t>(decoded.payload.issued_at_seconds)}
        };
        if (context.now < issued_at) {
            return {detail::retry_token_error("retry token issue time is in the future")};
        }
        if (context.now - issued_at > config_.lifetime) {
            return {detail::retry_token_error("retry token expired")};
        }
        if (decoded.payload.host != context.remote.host || decoded.payload.port != context.remote.port) {
            return {detail::retry_token_error("retry token peer address mismatch")};
        }
        if (!detail::constant_time_equal(
                std::span<const std::byte>{
                    decoded.payload.original_destination_connection_id.bytes.data(),
                    decoded.payload.original_destination_connection_id.bytes.size()
                },
                std::span<const std::byte>{
                    context.original_destination_connection_id.bytes.data(),
                    context.original_destination_connection_id.bytes.size()
                })) {
            return {detail::retry_token_error("retry token original destination connection ID mismatch")};
        }

        return {{}};
    }

private:
    retry_integrity_provider integrity_provider_{};
    retry_token_validator_config config_{};
};

} // namespace flowq::quic
