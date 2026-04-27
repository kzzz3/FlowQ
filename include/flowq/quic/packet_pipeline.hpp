#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>
#include <flowq/quic/ack_loss.hpp>
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

class packet_protector {
public:
    virtual ~packet_protector() = default;

    [[nodiscard]] virtual protection_level level() const noexcept = 0;
    [[nodiscard]] virtual packet_protection_result protect(std::span<const std::byte> plaintext) const = 0;
    [[nodiscard]] virtual packet_protection_result unprotect(std::span<const std::byte> protected_payload) const = 0;
};

class plaintext_packet_protector final : public packet_protector {
public:
    [[nodiscard]] protection_level level() const noexcept override {
        return protection_level::none;
    }

    [[nodiscard]] packet_protection_result protect(std::span<const std::byte> plaintext) const override {
        return {flowq::buffer{plaintext}, {}};
    }

    [[nodiscard]] packet_protection_result unprotect(std::span<const std::byte> protected_payload) const override {
        return {flowq::buffer{protected_payload}, {}};
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
    const packet_protector* protector{};
    packet_pipeline_config config{};
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

namespace detail {

[[nodiscard]] inline flowq::error pipeline_error(const char* message) {
    return flowq::error{flowq::error_code::protocol_error, message};
}

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
        return std::byte{0xc0};
    case long_packet_type::handshake:
        return std::byte{0xe0};
    }
    return std::byte{0xc0};
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

[[nodiscard]] inline packet_number_space space_for_header(const packet_header& header) {
    return std::holds_alternative<initial_header>(header) ? packet_number_space::initial : packet_number_space::handshake;
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

} // namespace detail

[[nodiscard]] inline assembled_packet assemble_long_packet(const packet_build_request& request) {
    if (request.protector == nullptr) {
        return {{}, request.number, protection_level::none, detail::pipeline_error("packet protector is required")};
    }
    if (request.number.value > 0xffff'ffffULL) {
        return {{}, request.number, request.protector->level(), detail::pipeline_error("packet number exceeds fixed M4a encoding")};
    }
    if (request.number.space != detail::space_for(request.type)) {
        return {{}, request.number, request.protector->level(), detail::pipeline_error("packet number space does not match packet type")};
    }
    if (!detail::protection_level_matches(request.type, request.protector->level())) {
        return {{}, request.number, request.protector->level(), detail::pipeline_error("packet protector level does not match packet type")};
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

    const auto protected_frames = request.protector->protect(frame_bytes);
    if (!protected_frames.ok()) {
        return {{}, request.number, request.protector->level(), protected_frames.error};
    }

    std::vector<std::byte> protected_payload;
    protected_payload.reserve(4 + protected_frames.payload.size());
    detail::append_packet_number(protected_payload, request.number.value);
    protected_payload.insert(
        protected_payload.end(),
        protected_frames.payload.data(),
        protected_frames.payload.data() + static_cast<std::ptrdiff_t>(protected_frames.payload.size()));

    packet_header header;
    if (request.type == long_packet_type::initial) {
        header = initial_header{
            detail::long_header_first_byte(request.type),
            request.version,
            request.destination_connection_id,
            request.source_connection_id,
            request.token,
            protected_payload.size(),
            flowq::buffer{protected_payload}
        };
    } else {
        header = handshake_header{
            detail::long_header_first_byte(request.type),
            request.version,
            request.destination_connection_id,
            request.source_connection_id,
            protected_payload.size(),
            flowq::buffer{protected_payload}
        };
    }

    auto encoded = encode_packet_header(header);
    if (!encoded.ok()) {
        return {{}, request.number, request.protector->level(), encoded.error};
    }
    if (encoded.payload.size() > request.config.max_datagram_size) {
        return {{}, request.number, request.protector->level(), detail::pipeline_error("QUIC datagram exceeds maximum size")};
    }

    return {std::move(encoded.payload), request.number, request.protector->level(), {}};
}

[[nodiscard]] inline parsed_packet parse_long_packet(std::span<const std::byte> datagram, const packet_protector* protector) {
    if (protector == nullptr) {
        return {{}, {}, {}, protection_level::none, {}, detail::pipeline_error("packet protector is required")};
    }

    auto decoded_header = decode_packet_header(datagram);
    if (!decoded_header.ok()) {
        return {{}, {}, {}, protector->level(), {}, decoded_header.error};
    }
    if (!std::holds_alternative<initial_header>(decoded_header.header) && !std::holds_alternative<handshake_header>(decoded_header.header)) {
        return {std::move(decoded_header.header), {}, {}, protector->level(), {}, detail::pipeline_error("unsupported packet type for frame parsing")};
    }

    const auto* protected_payload = detail::protected_payload_for(decoded_header.header);
    if (protected_payload == nullptr || protected_payload->size() < 4) {
        return {std::move(decoded_header.header), {}, {}, protector->level(), {}, detail::pipeline_error("packet payload is missing fixed packet number")};
    }

    const std::span<const std::byte> payload{protected_payload->data(), protected_payload->size()};
    const auto number = packet_number{detail::space_for_header(decoded_header.header), detail::read_packet_number(std::span<const std::byte, 4>{payload.data(), 4})};

    const auto plaintext = protector->unprotect(payload.subspan(4));
    if (!plaintext.ok()) {
        return {std::move(decoded_header.header), number, number.space, protector->level(), {}, plaintext.error};
    }

    auto decoded_frames = decode_frames(plaintext.payload);
    if (!decoded_frames.ok()) {
        return {std::move(decoded_header.header), number, number.space, protector->level(), {}, decoded_frames.error};
    }

    return {std::move(decoded_header.header), number, number.space, protector->level(), std::move(decoded_frames.frames), {}};
}

[[nodiscard]] inline parsed_packet parse_long_packet(const flowq::buffer& datagram, const packet_protector& protector) {
    return parse_long_packet(std::span<const std::byte>{datagram.data(), datagram.size()}, &protector);
}

[[nodiscard]] inline parsed_packet parse_long_packet(const flowq::buffer& datagram, const packet_protector* protector) {
    return parse_long_packet(std::span<const std::byte>{datagram.data(), datagram.size()}, protector);
}

} // namespace flowq::quic
