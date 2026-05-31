#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>
#include <flowq/quic/ack_loss.hpp>
#include <flowq/quic/crypto_provider.hpp>
#include <flowq/quic/frame.hpp>
#include <flowq/quic/packet_header.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace flowq::quic {

enum class protection_level {
    none,
    initial,
    handshake,
    application
};

enum class packet_security_level {
    test_only,
    authenticated_encrypted
};

enum class packet_protection_policy {
    production_required,
#if defined(FLOWQ_ENABLE_TEST_PACKET_PROTECTION_BYPASS)
    test_allowed,
#endif
};

enum class long_packet_type {
    initial,
    handshake
};

struct packet_number {
    packet_number_space space{};
    std::uint64_t value{};
};

struct packet_pipeline_config {
    std::size_t max_datagram_size{1200};
};

struct packet_protection_result {
    flowq::buffer payload;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

struct packet_protection_context {
    packet_number number{};
    std::span<const std::byte> associated_data;
};

class packet_protector {
public:
    virtual ~packet_protector() = default;

    [[nodiscard]] virtual protection_level level() const noexcept = 0;
    [[nodiscard]] virtual packet_security_level security_level() const noexcept = 0;
    [[nodiscard]] virtual crypto_provider_status provider_status() const noexcept {
        return crypto_provider_status::unavailable();
    }
    [[nodiscard]] virtual std::size_t protection_overhead() const noexcept {
        return 0;
    }
    [[nodiscard]] virtual bool header_protection_enabled() const noexcept {
        return false;
    }
    [[nodiscard]] virtual header_protection_mask_result header_protection_mask(std::span<const std::byte> sample) const {
        (void)sample;
        return {{}, flowq::error{flowq::error_code::protocol_error, "packet protector does not provide header protection"}};
    }
    [[nodiscard]] virtual packet_protection_result protect(std::span<const std::byte> plaintext) const = 0;
    [[nodiscard]] virtual packet_protection_result unprotect(std::span<const std::byte> protected_payload) const = 0;
    [[nodiscard]] virtual packet_protection_result protect(
        const packet_protection_context& context,
        std::span<const std::byte> plaintext) const {
        (void)context;
        return protect(plaintext);
    }
    [[nodiscard]] virtual packet_protection_result unprotect(
        const packet_protection_context& context,
        std::span<const std::byte> protected_payload) const {
        (void)context;
        return unprotect(protected_payload);
    }
};

struct packet_build_request {
    long_packet_type type{};
    std::uint32_t version{1};
    connection_id destination_connection_id;
    connection_id source_connection_id;
    flowq::buffer token;
    packet_number number{};
    std::vector<frame> frames;
    /// @pre The protector must outlive this request.
    const packet_protector* protector{};
    packet_pipeline_config config{};
    packet_protection_policy protection_policy{packet_protection_policy::production_required};
};

struct application_packet_build_request {
    connection_id destination_connection_id;
    packet_number number{};
    std::vector<frame> frames;
    /// @pre The protector must outlive this request.
    const packet_protector* protector{};
    packet_pipeline_config config{};
    packet_protection_policy protection_policy{packet_protection_policy::production_required};
};

struct assembled_packet {
    flowq::buffer datagram;
    packet_number number{};
    protection_level protection{};
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

struct parsed_packet {
    packet_header header{};
    packet_number number{};
    packet_number_space space{};
    protection_level protection{};
    std::vector<frame> frames;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

struct frame_payload_budget_selection {
    std::vector<frame> frames;
    std::size_t encoded_size{};
    std::size_t next_index{};
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

struct packet_size_result {
    std::size_t value{};
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

namespace detail {

[[nodiscard]] inline flowq::error pipeline_error(const char* message) {
    return flowq::error{flowq::error_code::protocol_error, message};
}

inline constexpr std::size_t fixed_packet_number_length = 4;
inline constexpr std::size_t minimum_packet_number_length = 1;

inline void append_packet_number(std::vector<std::byte>& output, std::uint64_t value) {
    output.push_back(static_cast<std::byte>((value >> 24U) & 0xffU));
    output.push_back(static_cast<std::byte>((value >> 16U) & 0xffU));
    output.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
    output.push_back(static_cast<std::byte>(value & 0xffU));
}

[[nodiscard]] inline std::uint64_t read_packet_number(std::span<const std::byte, 4> input) {
    return (static_cast<std::uint64_t>(input[0]) << 24U) |
        (static_cast<std::uint64_t>(input[1]) << 16U) |
        (static_cast<std::uint64_t>(input[2]) << 8U) |
        static_cast<std::uint64_t>(input[3]);
}

[[nodiscard]] inline std::byte long_header_first_byte(long_packet_type type) {
    switch (type) {
    case long_packet_type::initial:
        return std::byte{0xc3};
    case long_packet_type::handshake:
        return std::byte{0xe3};
    }
    return std::byte{0xc3};
}

[[nodiscard]] inline std::uint64_t read_packet_number(std::span<const std::byte> input) {
    std::uint64_t value{};
    for (auto byte : input) {
        value = (value << 8U) | static_cast<std::uint64_t>(byte);
    }
    return value;
}

[[nodiscard]] inline packet_number_space space_for(long_packet_type type) {
    return type == long_packet_type::initial ? packet_number_space::initial : packet_number_space::handshake;
}

[[nodiscard]] inline protection_level expected_protection_for(long_packet_type type) {
    return type == long_packet_type::initial ? protection_level::initial : protection_level::handshake;
}

[[nodiscard]] inline bool protection_level_matches(long_packet_type type, protection_level level) {
    return level == protection_level::none || level == expected_protection_for(type);
}

[[nodiscard]] inline bool application_protection_level_matches(protection_level level) {
    return level == protection_level::none || level == protection_level::application;
}

[[nodiscard]] inline bool production_protection_satisfied(const packet_protector& protector, packet_protection_policy policy) noexcept {
#if defined(FLOWQ_ENABLE_TEST_PACKET_PROTECTION_BYPASS)
    if (policy == packet_protection_policy::test_allowed) {
        return true;
    }
#else
    (void)policy;
#endif
    return protector.security_level() == packet_security_level::authenticated_encrypted && protector.provider_status().packet_protection_ready();
}

[[nodiscard]] inline flowq::error validate_protection_policy(const packet_protector& protector, packet_protection_policy policy) {
    if (!production_protection_satisfied(protector, policy)) {
        return pipeline_error("production packet protection requires authenticated encrypted external crypto provider");
    }
    return {};
}

[[nodiscard]] inline packet_number_space space_for_header(const packet_header& header) {
    return std::holds_alternative<initial_header>(header) ? packet_number_space::initial : packet_number_space::handshake;
}

[[nodiscard]] inline protection_level expected_protection_for_header(const packet_header& header) {
    return std::holds_alternative<initial_header>(header) ? protection_level::initial : protection_level::handshake;
}

[[nodiscard]] inline bool protection_level_matches_header(const packet_header& header, protection_level level) {
    return level == protection_level::none || level == expected_protection_for_header(header);
}

[[nodiscard]] inline const flowq::buffer* protected_payload_for(const packet_header& header) {
    if (const auto* initial = std::get_if<initial_header>(&header)) {
        return &initial->protected_payload;
    }
    if (const auto* handshake = std::get_if<handshake_header>(&header)) {
        return &handshake->protected_payload;
    }
    return nullptr;
}

[[nodiscard]] inline packet_size_result long_packet_wire_size(std::span<const std::byte> datagram) {
    if (datagram.empty()) {
        return {0, pipeline_error("empty long-header datagram")};
    }
    const auto first = static_cast<std::uint8_t>(datagram[0]);
    if ((first & 0x80U) == 0) {
        return {0, pipeline_error("datagram does not start with a long-header packet")};
    }
    if ((first & 0x40U) == 0) {
        return {0, pipeline_error("long header fixed bit is not set")};
    }

    std::size_t offset = 1U + 4U;
    connection_id destination_connection_id;
    connection_id source_connection_id;
    if (!read_connection_id(datagram, offset, destination_connection_id) || !read_connection_id(datagram, offset, source_connection_id)) {
        return {0, pipeline_error("truncated long-header connection ID")};
    }

    if ((first & 0x30U) == 0x00U) {
        std::uint64_t token_length = 0;
        if (!read_packet_varint_at(datagram, offset, token_length)) {
            return {0, pipeline_error("truncated Initial token length")};
        }
        if (token_length > datagram.size() - offset) {
            return {0, pipeline_error("truncated Initial token")};
        }
        offset += static_cast<std::size_t>(token_length);
    } else if ((first & 0x30U) != 0x20U) {
        return {0, pipeline_error("unsupported coalesced long-header packet type")};
    }

    std::uint64_t payload_length = 0;
    if (!read_packet_varint_at(datagram, offset, payload_length)) {
        return {0, pipeline_error("truncated long-header packet length")};
    }
    if (payload_length > datagram.size() - offset) {
        return {0, pipeline_error("truncated long-header protected payload")};
    }

    return {offset + static_cast<std::size_t>(payload_length), {}};
}

[[nodiscard]] inline packet_protection_result protect_payload(
    const packet_protector& protector,
    const packet_protection_context& context,
    std::span<const std::byte> plaintext) {
    return protector.protect(context, plaintext);
}

[[nodiscard]] inline packet_protection_result unprotect_payload(
    const packet_protector& protector,
    const packet_protection_context& context,
    std::span<const std::byte> protected_payload) {
    return protector.unprotect(context, protected_payload);
}

[[nodiscard]] inline packet_protection_result protect_payload(
    const packet_protector& protector,
    std::span<const std::byte> plaintext) {
    return protector.protect(plaintext);
}

[[nodiscard]] inline packet_protection_result unprotect_payload(
    const packet_protector& protector,
    std::span<const std::byte> protected_payload) {
    return protector.unprotect(protected_payload);
}

[[nodiscard]] inline flowq::error apply_long_header_protection(
    std::span<std::byte> datagram,
    std::size_t protected_payload_size,
    const packet_protector& protector) {
    if (protected_payload_size < fixed_packet_number_length + 16U || datagram.size() < protected_payload_size) {
        return pipeline_error("packet payload too short for header protection sample");
    }

    const auto packet_number_offset = datagram.size() - protected_payload_size;
    const auto sample_offset = packet_number_offset + fixed_packet_number_length;
    if (datagram.size() - sample_offset < 16U) {
        return pipeline_error("packet payload too short for header protection sample");
    }

    const auto packet_number_length = static_cast<std::size_t>(static_cast<std::uint8_t>(datagram[0]) & 0x03U) + 1U;
    if (packet_number_length != fixed_packet_number_length) {
        return pipeline_error("FlowQ packet pipeline requires fixed 4-byte packet numbers");
    }
    auto mask = protector.header_protection_mask(std::span<const std::byte>{datagram.data() + static_cast<std::ptrdiff_t>(sample_offset), 16});
    if (!mask.ok()) {
        return mask.error;
    }

    datagram[0] ^= static_cast<std::byte>(static_cast<std::uint8_t>(mask.mask[0]) & 0x0fU);
    for (std::size_t index = 0; index < packet_number_length; ++index) {
        datagram[packet_number_offset + index] ^= mask.mask[index + 1U];
    }

    return {};
}

[[nodiscard]] inline flowq::error remove_long_header_protection(
    std::vector<std::byte>& datagram,
    std::size_t protected_payload_size,
    const packet_protector& protector) {
    if (protected_payload_size < minimum_packet_number_length + 16U || datagram.size() < protected_payload_size) {
        return pipeline_error("packet payload too short for header protection sample");
    }

    const auto packet_number_offset = datagram.size() - protected_payload_size;
    const auto sample_offset = packet_number_offset + fixed_packet_number_length;
    if (datagram.size() - sample_offset < 16U) {
        return pipeline_error("packet payload too short for header protection sample");
    }

    auto mask = protector.header_protection_mask(std::span<const std::byte>{datagram.data() + static_cast<std::ptrdiff_t>(sample_offset), 16});
    if (!mask.ok()) {
        return mask.error;
    }

    datagram[0] ^= static_cast<std::byte>(static_cast<std::uint8_t>(mask.mask[0]) & 0x0fU);
    const auto packet_number_length = static_cast<std::size_t>(static_cast<std::uint8_t>(datagram[0]) & 0x03U) + 1U;
    if (protected_payload_size < packet_number_length + 16U) {
        return pipeline_error("packet payload too short for header protection sample");
    }
    for (std::size_t index = 0; index < packet_number_length; ++index) {
        datagram[packet_number_offset + index] ^= mask.mask[index + 1U];
    }

    return {};
}

[[nodiscard]] inline flowq::error apply_short_header_protection(
    std::span<std::byte> datagram,
    std::size_t destination_connection_id_length,
    const packet_protector& protector) {
    const auto packet_number_offset = 1U + destination_connection_id_length;
    if (datagram.size() < packet_number_offset + fixed_packet_number_length + 16U) {
        return pipeline_error("short packet payload too short for header protection sample");
    }

    const auto packet_number_length = static_cast<std::size_t>(static_cast<std::uint8_t>(datagram[0]) & 0x03U) + 1U;
    if (packet_number_length != fixed_packet_number_length) {
        return pipeline_error("FlowQ packet pipeline requires fixed 4-byte packet numbers");
    }
    const auto sample_offset = packet_number_offset + fixed_packet_number_length;
    auto mask = protector.header_protection_mask(std::span<const std::byte>{datagram.data() + static_cast<std::ptrdiff_t>(sample_offset), 16});
    if (!mask.ok()) {
        return mask.error;
    }

    datagram[0] ^= static_cast<std::byte>(static_cast<std::uint8_t>(mask.mask[0]) & 0x1fU);
    for (std::size_t index = 0; index < packet_number_length; ++index) {
        datagram[packet_number_offset + index] ^= mask.mask[index + 1U];
    }

    return {};
}

[[nodiscard]] inline flowq::error remove_short_header_protection(
    std::vector<std::byte>& datagram,
    std::size_t destination_connection_id_length,
    const packet_protector& protector) {
    const auto packet_number_offset = 1U + destination_connection_id_length;
    if (datagram.size() < packet_number_offset + minimum_packet_number_length + 16U) {
        return pipeline_error("short packet payload too short for header protection sample");
    }

    const auto sample_offset = packet_number_offset + fixed_packet_number_length;
    if (datagram.size() < sample_offset + 16U) {
        return pipeline_error("short packet payload too short for header protection sample");
    }

    auto mask = protector.header_protection_mask(std::span<const std::byte>{datagram.data() + static_cast<std::ptrdiff_t>(sample_offset), 16});
    if (!mask.ok()) {
        return mask.error;
    }

    datagram[0] ^= static_cast<std::byte>(static_cast<std::uint8_t>(mask.mask[0]) & 0x1fU);
    const auto packet_number_length = static_cast<std::size_t>(static_cast<std::uint8_t>(datagram[0]) & 0x03U) + 1U;
    if (datagram.size() < packet_number_offset + packet_number_length + 16U) {
        return pipeline_error("short packet payload too short for header protection sample");
    }
    for (std::size_t index = 0; index < packet_number_length; ++index) {
        datagram[packet_number_offset + index] ^= mask.mask[index + 1U];
    }

    return {};
}

} // namespace detail

[[nodiscard]] inline frame_payload_budget_selection select_frames_for_payload_budget(std::span<const frame> candidates, std::size_t budget) {
    frame_payload_budget_selection result{};
    result.frames.reserve(candidates.size());

    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const auto encoded = std::visit(
            [](const auto& concrete_frame) {
                return encode_frame(concrete_frame);
            },
            candidates[index]);
        if (!encoded.ok()) {
            result.error = encoded.error;
            result.next_index = index;
            return result;
        }

        if (encoded.payload.size() > budget - result.encoded_size) {
            result.next_index = index;
            return result;
        }

        result.encoded_size += encoded.payload.size();
        result.frames.push_back(candidates[index]);
        result.next_index = index + 1;
    }

    return result;
}

[[nodiscard]] inline assembled_packet assemble_long_packet(const packet_build_request& request) {
    if (request.protector == nullptr) {
        return {{}, request.number, protection_level::none, detail::pipeline_error("packet protector is required")};
    }
    if (request.number.value > 0xffff'ffffULL) {
        return {{}, request.number, request.protector->level(), detail::pipeline_error("packet number exceeds supported long-header encoding")};
    }
    if (request.number.space != detail::space_for(request.type)) {
        return {{}, request.number, request.protector->level(), detail::pipeline_error("packet number space does not match packet type")};
    }
    if (!detail::protection_level_matches(request.type, request.protector->level())) {
        return {{}, request.number, request.protector->level(), detail::pipeline_error("packet protector level does not match packet type")};
    }
    if (auto error = detail::validate_protection_policy(*request.protector, request.protection_policy); !error.ok()) {
        return {{}, request.number, request.protector->level(), error};
    }

    std::vector<std::byte> frame_bytes;
    for (const auto& frame : request.frames) {
        const auto encoded = std::visit(
            [](const auto& concrete_frame) {
                return encode_frame(concrete_frame);
            },
            frame);
        if (!encoded.ok()) {
            return {{}, request.number, request.protector->level(), encoded.error};
        }
        frame_bytes.insert(frame_bytes.end(), encoded.payload.data(), encoded.payload.data() + static_cast<std::ptrdiff_t>(encoded.payload.size()));
    }

    const auto protected_payload_length = detail::fixed_packet_number_length + frame_bytes.size() + request.protector->protection_overhead();
    auto length_accounting_payload = flowq::buffer{std::vector<std::byte>(protected_payload_length, std::byte{0x00})};

    packet_header header;
    if (request.type == long_packet_type::initial) {
        header = initial_header{
            detail::long_header_first_byte(request.type),
            request.version,
            request.destination_connection_id,
            request.source_connection_id,
            request.token,
            protected_payload_length,
            length_accounting_payload
        };
    } else {
        header = handshake_header{
            detail::long_header_first_byte(request.type),
            request.version,
            request.destination_connection_id,
            request.source_connection_id,
            protected_payload_length,
            std::move(length_accounting_payload)
        };
    }

    std::vector<std::byte> packet_number_bytes;
    packet_number_bytes.reserve(detail::fixed_packet_number_length);
    detail::append_packet_number(packet_number_bytes, request.number.value);

    const auto aad_prefix = encode_packet_header(header);
    if (!aad_prefix.ok()) {
        return {{}, request.number, request.protector->level(), aad_prefix.error};
    }
    if (aad_prefix.payload.size() < protected_payload_length) {
        return {{}, request.number, request.protector->level(), detail::pipeline_error("encoded long header is shorter than protected payload")};
    }
    const auto header_prefix_size = aad_prefix.payload.size() - protected_payload_length;
    std::vector<std::byte> associated_data{
        aad_prefix.payload.data(),
        aad_prefix.payload.data() + static_cast<std::ptrdiff_t>(header_prefix_size)};
    associated_data.insert(associated_data.end(), packet_number_bytes.begin(), packet_number_bytes.end());

    const auto protected_frames = detail::protect_payload(
        *request.protector,
        packet_protection_context{
            request.number,
            std::span<const std::byte>{associated_data.data(), associated_data.size()}
        },
        frame_bytes);
    if (!protected_frames.ok()) {
        return {{}, request.number, request.protector->level(), protected_frames.error};
    }

    std::vector<std::byte> protected_payload;
    protected_payload.reserve(packet_number_bytes.size() + protected_frames.payload.size());
    protected_payload.insert(protected_payload.end(), packet_number_bytes.begin(), packet_number_bytes.end());
    protected_payload.insert(
        protected_payload.end(),
        protected_frames.payload.data(),
        protected_frames.payload.data() + static_cast<std::ptrdiff_t>(protected_frames.payload.size()));

    if (auto* initial = std::get_if<initial_header>(&header)) {
        initial->protected_payload = flowq::buffer{protected_payload};
    } else if (auto* handshake = std::get_if<handshake_header>(&header)) {
        handshake->protected_payload = flowq::buffer{protected_payload};
    }

    auto encoded = encode_packet_header(header);
    if (!encoded.ok()) {
        return {{}, request.number, request.protector->level(), encoded.error};
    }
    if (request.protector->header_protection_enabled()) {
        auto datagram = std::vector<std::byte>{
            encoded.payload.data(),
            encoded.payload.data() + static_cast<std::ptrdiff_t>(encoded.payload.size())};
        if (auto error = detail::apply_long_header_protection(
                std::span<std::byte>{datagram.data(), datagram.size()},
                protected_payload.size(),
                *request.protector); !error.ok()) {
            return {{}, request.number, request.protector->level(), error};
        }
        encoded.payload = flowq::buffer{std::move(datagram)};
    }
    if (encoded.payload.size() > request.config.max_datagram_size) {
        return {{}, request.number, request.protector->level(), detail::pipeline_error("QUIC datagram exceeds maximum size")};
    }

    return {std::move(encoded.payload), request.number, request.protector->level(), {}};
}

[[nodiscard]] inline assembled_packet assemble_application_packet(const application_packet_build_request& request) {
    if (request.protector == nullptr) {
        return {{}, request.number, protection_level::none, detail::pipeline_error("packet protector is required")};
    }
    if (request.number.value > 0xffff'ffffULL) {
        return {{}, request.number, request.protector->level(), detail::pipeline_error("packet number exceeds supported application-header encoding")};
    }
    if (request.number.space != packet_number_space::application) {
        return {{}, request.number, request.protector->level(), detail::pipeline_error("packet number space does not match short-header packet")};
    }
    if (!detail::application_protection_level_matches(request.protector->level())) {
        return {{}, request.number, request.protector->level(), detail::pipeline_error("packet protector level does not match short-header packet")};
    }
    if (auto error = detail::validate_protection_policy(*request.protector, request.protection_policy); !error.ok()) {
        return {{}, request.number, request.protector->level(), error};
    }

    std::vector<std::byte> frame_bytes;
    for (const auto& frame : request.frames) {
        const auto encoded = std::visit(
            [](const auto& concrete_frame) {
                return encode_frame(concrete_frame);
            },
            frame);
        if (!encoded.ok()) {
            return {{}, request.number, request.protector->level(), encoded.error};
        }
        frame_bytes.insert(frame_bytes.end(), encoded.payload.data(), encoded.payload.data() + static_cast<std::ptrdiff_t>(encoded.payload.size()));
    }

    const auto protected_payload_length = frame_bytes.size() + request.protector->protection_overhead();
    auto length_accounting_payload = flowq::buffer{std::vector<std::byte>(protected_payload_length, std::byte{0x00})};
    short_header header{
        std::byte{0x43},
        request.destination_connection_id,
        true,
        false,
        false,
        detail::fixed_packet_number_length,
        request.number.value,
        std::move(length_accounting_payload)
    };

    std::vector<std::byte> packet_number_bytes;
    packet_number_bytes.reserve(detail::fixed_packet_number_length);
    detail::append_packet_number(packet_number_bytes, request.number.value);

    const auto aad_prefix = encode_packet_header(packet_header{header});
    if (!aad_prefix.ok()) {
        return {{}, request.number, request.protector->level(), aad_prefix.error};
    }
    if (aad_prefix.payload.size() < protected_payload_length) {
        return {{}, request.number, request.protector->level(), detail::pipeline_error("encoded short header is shorter than protected payload")};
    }
    const auto header_prefix_size = aad_prefix.payload.size() - protected_payload_length;
    std::vector<std::byte> associated_data{
        aad_prefix.payload.data(),
        aad_prefix.payload.data() + static_cast<std::ptrdiff_t>(header_prefix_size)};

    const auto protected_frames = detail::protect_payload(
        *request.protector,
        packet_protection_context{
            request.number,
            std::span<const std::byte>{associated_data.data(), associated_data.size()}
        },
        frame_bytes);
    if (!protected_frames.ok()) {
        return {{}, request.number, request.protector->level(), protected_frames.error};
    }

    header.protected_payload = std::move(protected_frames.payload);
    auto encoded = encode_packet_header(packet_header{header});
    if (!encoded.ok()) {
        return {{}, request.number, request.protector->level(), encoded.error};
    }
    if (request.protector->header_protection_enabled()) {
        auto datagram = std::vector<std::byte>{
            encoded.payload.data(),
            encoded.payload.data() + static_cast<std::ptrdiff_t>(encoded.payload.size())};
        if (auto error = detail::apply_short_header_protection(
                std::span<std::byte>{datagram.data(), datagram.size()},
                request.destination_connection_id.bytes.size(),
                *request.protector); !error.ok()) {
            return {{}, request.number, request.protector->level(), error};
        }
        encoded.payload = flowq::buffer{std::move(datagram)};
    }
    if (encoded.payload.size() > request.config.max_datagram_size) {
        return {{}, request.number, request.protector->level(), detail::pipeline_error("QUIC datagram exceeds maximum size")};
    }

    return {std::move(encoded.payload), request.number, request.protector->level(), {}};
}

[[nodiscard]] inline parsed_packet parse_long_packet(
    std::span<const std::byte> datagram,
    const packet_protector* protector,
    packet_protection_policy protection_policy = packet_protection_policy::production_required) {
    if (protector == nullptr) {
        return {{}, {}, {}, protection_level::none, {}, detail::pipeline_error("packet protector is required")};
    }
    if (auto error = detail::validate_protection_policy(*protector, protection_policy); !error.ok()) {
        return {{}, {}, {}, protector->level(), {}, error};
    }

    std::span<const std::byte> decoded_datagram = datagram;
    std::vector<std::byte> unmasked_datagram;

    auto decoded_header = decode_packet_header(decoded_datagram);
    if (!decoded_header.ok()) {
        return {{}, {}, {}, protector->level(), {}, decoded_header.error};
    }
    if (protector->header_protection_enabled()) {
        const auto* masked_payload = detail::protected_payload_for(decoded_header.header);
        if (masked_payload == nullptr) {
            return {std::move(decoded_header.header), {}, {}, protector->level(), {}, detail::pipeline_error("unsupported packet type for header protection removal")};
        }
        unmasked_datagram.assign(datagram.begin(), datagram.end());
        if (auto error = detail::remove_long_header_protection(unmasked_datagram, masked_payload->size(), *protector); !error.ok()) {
            return {std::move(decoded_header.header), {}, {}, protector->level(), {}, error};
        }
        decoded_datagram = std::span<const std::byte>{unmasked_datagram.data(), unmasked_datagram.size()};
        decoded_header = decode_packet_header(decoded_datagram);
        if (!decoded_header.ok()) {
            return {{}, {}, {}, protector->level(), {}, decoded_header.error};
        }
    }
    if (!std::holds_alternative<initial_header>(decoded_header.header) && !std::holds_alternative<handshake_header>(decoded_header.header)) {
        return {std::move(decoded_header.header), {}, {}, protector->level(), {}, detail::pipeline_error("unsupported packet type for frame parsing")};
    }
    if (!detail::protection_level_matches_header(decoded_header.header, protector->level())) {
        return {std::move(decoded_header.header), {}, {}, protector->level(), {}, detail::pipeline_error("packet protector level does not match decoded long-header packet type")};
    }

    const auto* protected_payload = detail::protected_payload_for(decoded_header.header);
    if (protected_payload == nullptr || protected_payload->size() < detail::minimum_packet_number_length) {
        return {std::move(decoded_header.header), {}, {}, protector->level(), {}, detail::pipeline_error("packet payload is missing fixed packet number")};
    }

    const std::span<const std::byte> payload{protected_payload->data(), protected_payload->size()};
    const auto packet_number_length = static_cast<std::size_t>(static_cast<std::uint8_t>(decoded_datagram[0]) & 0x03U) + 1U;
    if (payload.size() < packet_number_length) {
        return {std::move(decoded_header.header), {}, {}, protector->level(), {}, detail::pipeline_error("packet payload is missing packet number")};
    }
    const auto number = packet_number{
        detail::space_for_header(decoded_header.header),
        detail::read_packet_number(payload.first(packet_number_length))};

    std::vector<std::byte> associated_data{
        decoded_datagram.begin(),
        decoded_datagram.begin() + static_cast<std::ptrdiff_t>(decoded_datagram.size() - protected_payload->size() + packet_number_length)};
    const auto plaintext = detail::unprotect_payload(
        *protector,
        packet_protection_context{
            number,
            std::span<const std::byte>{associated_data.data(), associated_data.size()}
        },
        payload.subspan(packet_number_length));
    if (!plaintext.ok()) {
        return {std::move(decoded_header.header), number, number.space, protector->level(), {}, plaintext.error};
    }

    auto decoded_frames = decode_frames(plaintext.payload);
    if (!decoded_frames.ok()) {
        return {std::move(decoded_header.header), number, number.space, protector->level(), {}, decoded_frames.error};
    }

    return {std::move(decoded_header.header), number, number.space, protector->level(), std::move(decoded_frames.frames), {}};
}

[[nodiscard]] inline parsed_packet parse_short_packet(
    std::span<const std::byte> datagram,
    std::size_t destination_connection_id_length,
    const packet_protector* protector,
    packet_protection_policy protection_policy = packet_protection_policy::production_required) {
    if (protector == nullptr) {
        return {{}, {}, {}, protection_level::none, {}, detail::pipeline_error("packet protector is required")};
    }
    if (!detail::application_protection_level_matches(protector->level())) {
        return {{}, {}, {}, protector->level(), {}, detail::pipeline_error("packet protector level does not match short-header packet")};
    }
    if (auto error = detail::validate_protection_policy(*protector, protection_policy); !error.ok()) {
        return {{}, {}, {}, protector->level(), {}, error};
    }
    std::span<const std::byte> decoded_datagram = datagram;
    std::vector<std::byte> unmasked_datagram;
    if (protector->header_protection_enabled()) {
        unmasked_datagram.assign(datagram.begin(), datagram.end());
        if (auto error = detail::remove_short_header_protection(unmasked_datagram, destination_connection_id_length, *protector); !error.ok()) {
            return {{}, {}, packet_number_space::application, protector->level(), {}, error};
        }
        decoded_datagram = std::span<const std::byte>{unmasked_datagram.data(), unmasked_datagram.size()};
    }

    auto decoded_header = decode_short_header(decoded_datagram, destination_connection_id_length);
    if (!decoded_header.ok()) {
        return {{}, {}, packet_number_space::application, protector->level(), {}, decoded_header.error};
    }
    const auto& header = std::get<short_header>(decoded_header.header);
    const auto number = packet_number{packet_number_space::application, header.truncated_packet_number};

    const auto packet_number_offset = 1U + destination_connection_id_length;
    std::vector<std::byte> associated_data{
        decoded_datagram.begin(),
        decoded_datagram.begin() + static_cast<std::ptrdiff_t>(packet_number_offset + header.packet_number_length)};
    const auto plaintext = detail::unprotect_payload(
        *protector,
        packet_protection_context{
            number,
            std::span<const std::byte>{associated_data.data(), associated_data.size()}
        },
        std::span<const std::byte>{header.protected_payload.data(), header.protected_payload.size()});
    if (!plaintext.ok()) {
        return {std::move(decoded_header.header), number, number.space, protector->level(), {}, plaintext.error};
    }

    auto decoded_frames = decode_frames(plaintext.payload);
    if (!decoded_frames.ok()) {
        return {std::move(decoded_header.header), number, number.space, protector->level(), {}, decoded_frames.error};
    }

    return {std::move(decoded_header.header), number, number.space, protector->level(), std::move(decoded_frames.frames), {}};
}

[[nodiscard]] inline parsed_packet parse_long_packet(
    const flowq::buffer& datagram,
    const packet_protector& protector,
    packet_protection_policy protection_policy = packet_protection_policy::production_required) {
    return parse_long_packet(std::span<const std::byte>{datagram.data(), datagram.size()}, &protector, protection_policy);
}

[[nodiscard]] inline parsed_packet parse_long_packet(
    const flowq::buffer& datagram,
    const packet_protector* protector,
    packet_protection_policy protection_policy = packet_protection_policy::production_required) {
    return parse_long_packet(std::span<const std::byte>{datagram.data(), datagram.size()}, protector, protection_policy);
}

[[nodiscard]] inline parsed_packet parse_short_packet(
    const flowq::buffer& datagram,
    std::size_t destination_connection_id_length,
    const packet_protector& protector,
    packet_protection_policy protection_policy = packet_protection_policy::production_required) {
    return parse_short_packet(std::span<const std::byte>{datagram.data(), datagram.size()}, destination_connection_id_length, &protector, protection_policy);
}

[[nodiscard]] inline parsed_packet parse_short_packet(
    const flowq::buffer& datagram,
    std::size_t destination_connection_id_length,
    const packet_protector* protector,
    packet_protection_policy protection_policy = packet_protection_policy::production_required) {
    return parse_short_packet(std::span<const std::byte>{datagram.data(), datagram.size()}, destination_connection_id_length, protector, protection_policy);
}

} // namespace flowq::quic
