#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>
#include <flowq/quic/varint.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace flowq::quic {

inline constexpr std::uint64_t transport_parameter_max_idle_timeout = 0x01;
inline constexpr std::uint64_t transport_parameter_max_udp_payload_size = 0x03;
inline constexpr std::uint64_t transport_parameter_initial_max_data = 0x04;
inline constexpr std::uint64_t transport_parameter_initial_max_stream_data_bidi_local = 0x05;
inline constexpr std::uint64_t transport_parameter_initial_max_stream_data_bidi_remote = 0x06;
inline constexpr std::uint64_t transport_parameter_initial_max_stream_data_uni = 0x07;
inline constexpr std::uint64_t transport_parameter_disable_active_migration = 0x0c;
inline constexpr std::uint64_t transport_parameter_active_connection_id_limit = 0x0e;

struct unknown_transport_parameter {
    std::uint64_t id{};
    flowq::buffer value;
};

struct transport_parameters {
    std::optional<std::uint64_t> max_idle_timeout;
    std::optional<std::uint64_t> max_udp_payload_size;
    std::optional<std::uint64_t> initial_max_data;
    std::optional<std::uint64_t> initial_max_stream_data_bidi_local;
    std::optional<std::uint64_t> initial_max_stream_data_bidi_remote;
    std::optional<std::uint64_t> initial_max_stream_data_uni;
    bool disable_active_migration{};
    std::optional<std::uint64_t> active_connection_id_limit;
    std::vector<unknown_transport_parameter> unknown;
};

struct transport_parameters_encode_result {
    flowq::buffer payload;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

struct transport_parameters_decode_result {
    transport_parameters parameters;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

namespace detail {

[[nodiscard]] inline flowq::error append_transport_varint(std::vector<std::byte>& output, std::uint64_t value) {
    const auto size = encoded_size(value);
    if (!size.ok()) {
        return size.error;
    }
    const auto offset = output.size();
    output.resize(offset + size.value);
    const auto encoded = encode_varint(value, std::span<std::byte>{output.data() + static_cast<std::ptrdiff_t>(offset), size.value});
    if (!encoded.ok()) {
        output.resize(offset);
        return encoded.error;
    }
    return {};
}

[[nodiscard]] inline transport_parameters_encode_result encode_transport_varint_value(std::uint64_t value) {
    std::vector<std::byte> output;
    auto error = append_transport_varint(output, value);
    if (!error.ok()) {
        return {{}, error};
    }
    return {flowq::buffer{output}, {}};
}

[[nodiscard]] inline flowq::error append_transport_parameter(std::vector<std::byte>& output, std::uint64_t id, const flowq::buffer& value) {
    const auto offset = output.size();
    auto error = append_transport_varint(output, id);
    if (!error.ok()) {
        output.resize(offset);
        return error;
    }
    error = append_transport_varint(output, value.size());
    if (!error.ok()) {
        output.resize(offset);
        return error;
    }
    output.insert(output.end(), value.data(), value.data() + static_cast<std::ptrdiff_t>(value.size()));
    return {};
}

[[nodiscard]] inline flowq::error append_numeric_transport_parameter(std::vector<std::byte>& output, std::uint64_t id, const std::optional<std::uint64_t>& value) {
    if (value.has_value()) {
        const auto encoded_value = encode_transport_varint_value(*value);
        if (!encoded_value.ok()) {
            return encoded_value.error;
        }
        return append_transport_parameter(output, id, encoded_value.payload);
    }
    return {};
}

[[nodiscard]] inline bool known_transport_parameter_id(std::uint64_t id) noexcept {
    switch (id) {
    case transport_parameter_max_idle_timeout:
    case transport_parameter_max_udp_payload_size:
    case transport_parameter_initial_max_data:
    case transport_parameter_initial_max_stream_data_bidi_local:
    case transport_parameter_initial_max_stream_data_bidi_remote:
    case transport_parameter_initial_max_stream_data_uni:
    case transport_parameter_disable_active_migration:
    case transport_parameter_active_connection_id_limit:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline bool already_seen(std::span<const std::uint64_t> seen, std::uint64_t id) noexcept {
    return std::find(seen.begin(), seen.end(), id) != seen.end();
}

[[nodiscard]] inline flowq::error remember_transport_parameter(std::vector<std::uint64_t>& emitted, std::uint64_t id) {
    if (id > max_varint) {
        return codec_error("QUIC transport parameter identifier exceeds varint maximum");
    }
    if (already_seen(emitted, id)) {
        return codec_error("duplicate QUIC transport parameter");
    }
    emitted.push_back(id);
    return {};
}

[[nodiscard]] inline flowq::error validate_transport_parameter_for_encode(std::uint64_t id, std::uint64_t value) {
    if (value > max_varint) {
        return codec_error("QUIC transport parameter value exceeds varint maximum");
    }
    if (id == transport_parameter_max_udp_payload_size && value < 1200) {
        return codec_error("max_udp_payload_size transport parameter must be at least 1200");
    }
    if (id == transport_parameter_active_connection_id_limit && value < 2) {
        return codec_error("active_connection_id_limit transport parameter must be at least 2");
    }
    return {};
}

[[nodiscard]] inline flowq::error append_checked_numeric_transport_parameter(
    std::vector<std::byte>& output,
    std::vector<std::uint64_t>& emitted,
    std::uint64_t id,
    const std::optional<std::uint64_t>& value) {
    if (!value.has_value()) {
        return {};
    }
    auto error = remember_transport_parameter(emitted, id);
    if (!error.ok()) {
        return error;
    }
    error = validate_transport_parameter_for_encode(id, *value);
    if (!error.ok()) {
        return error;
    }
    return append_numeric_transport_parameter(output, id, value);
}

[[nodiscard]] inline std::optional<std::uint64_t> decode_parameter_varint(std::span<const std::byte> value) {
    const auto decoded = decode_varint(value);
    if (!decoded.ok() || decoded.bytes_read != value.size()) {
        return std::nullopt;
    }
    return decoded.value;
}

[[nodiscard]] inline flowq::error assign_numeric_parameter(
    std::optional<std::uint64_t>& target,
    std::span<const std::byte> value,
    const char* message) {
    const auto decoded = decode_parameter_varint(value);
    if (!decoded.has_value()) {
        return codec_error(message);
    }
    target = *decoded;
    return {};
}

} // namespace detail

[[nodiscard]] inline transport_parameters_encode_result encode_transport_parameters(const transport_parameters& parameters) {
    std::vector<std::byte> output;
    std::vector<std::uint64_t> emitted;

    auto error = detail::append_checked_numeric_transport_parameter(output, emitted, transport_parameter_max_idle_timeout, parameters.max_idle_timeout);
    if (!error.ok()) {
        return {{}, error};
    }
    error = detail::append_checked_numeric_transport_parameter(output, emitted, transport_parameter_max_udp_payload_size, parameters.max_udp_payload_size);
    if (!error.ok()) {
        return {{}, error};
    }
    error = detail::append_checked_numeric_transport_parameter(output, emitted, transport_parameter_initial_max_data, parameters.initial_max_data);
    if (!error.ok()) {
        return {{}, error};
    }
    error = detail::append_checked_numeric_transport_parameter(output, emitted, transport_parameter_initial_max_stream_data_bidi_local, parameters.initial_max_stream_data_bidi_local);
    if (!error.ok()) {
        return {{}, error};
    }
    error = detail::append_checked_numeric_transport_parameter(output, emitted, transport_parameter_initial_max_stream_data_bidi_remote, parameters.initial_max_stream_data_bidi_remote);
    if (!error.ok()) {
        return {{}, error};
    }
    error = detail::append_checked_numeric_transport_parameter(output, emitted, transport_parameter_initial_max_stream_data_uni, parameters.initial_max_stream_data_uni);
    if (!error.ok()) {
        return {{}, error};
    }
    if (parameters.disable_active_migration) {
        error = detail::remember_transport_parameter(emitted, transport_parameter_disable_active_migration);
        if (!error.ok()) {
            return {{}, error};
        }
        error = detail::append_transport_parameter(output, transport_parameter_disable_active_migration, flowq::buffer{});
        if (!error.ok()) {
            return {{}, error};
        }
    }
    error = detail::append_checked_numeric_transport_parameter(output, emitted, transport_parameter_active_connection_id_limit, parameters.active_connection_id_limit);
    if (!error.ok()) {
        return {{}, error};
    }
    for (const auto& unknown : parameters.unknown) {
        if (detail::known_transport_parameter_id(unknown.id)) {
            return {{}, codec_error("unknown transport parameter entry uses a known identifier")};
        }
        error = detail::remember_transport_parameter(emitted, unknown.id);
        if (!error.ok()) {
            return {{}, error};
        }
        error = detail::append_transport_parameter(output, unknown.id, unknown.value);
        if (!error.ok()) {
            return {{}, error};
        }
    }
    return {flowq::buffer{output}, {}};
}

[[nodiscard]] inline transport_parameters_decode_result decode_transport_parameters(std::span<const std::byte> input) {
    transport_parameters result{};
    std::vector<std::uint64_t> seen;
    std::size_t offset = 0;
    while (offset < input.size()) {
        const auto decoded_id = decode_varint(input.subspan(offset));
        if (!decoded_id.ok()) {
            return {{}, decoded_id.error};
        }
        offset += decoded_id.bytes_read;
        const auto decoded_length = decode_varint(input.subspan(offset));
        if (!decoded_length.ok()) {
            return {{}, decoded_length.error};
        }
        offset += decoded_length.bytes_read;
        if (decoded_length.value > input.size() - offset) {
            return {{}, codec_error("truncated QUIC transport parameter value")};
        }

        const auto id = decoded_id.value;
        if (detail::already_seen(seen, id)) {
            return {{}, codec_error("duplicate QUIC transport parameter")};
        }
        seen.push_back(id);

        const auto length = static_cast<std::size_t>(decoded_length.value);
        const auto value = input.subspan(offset, length);
        offset += length;

        flowq::error error{};
        switch (id) {
        case transport_parameter_max_idle_timeout:
            error = detail::assign_numeric_parameter(result.max_idle_timeout, value, "malformed max_idle_timeout transport parameter");
            break;
        case transport_parameter_max_udp_payload_size:
            error = detail::assign_numeric_parameter(result.max_udp_payload_size, value, "malformed max_udp_payload_size transport parameter");
            if (error.ok() && *result.max_udp_payload_size < 1200) {
                error = codec_error("max_udp_payload_size transport parameter must be at least 1200");
            }
            break;
        case transport_parameter_initial_max_data:
            error = detail::assign_numeric_parameter(result.initial_max_data, value, "malformed initial_max_data transport parameter");
            break;
        case transport_parameter_initial_max_stream_data_bidi_local:
            error = detail::assign_numeric_parameter(result.initial_max_stream_data_bidi_local, value, "malformed initial_max_stream_data_bidi_local transport parameter");
            break;
        case transport_parameter_initial_max_stream_data_bidi_remote:
            error = detail::assign_numeric_parameter(result.initial_max_stream_data_bidi_remote, value, "malformed initial_max_stream_data_bidi_remote transport parameter");
            break;
        case transport_parameter_initial_max_stream_data_uni:
            error = detail::assign_numeric_parameter(result.initial_max_stream_data_uni, value, "malformed initial_max_stream_data_uni transport parameter");
            break;
        case transport_parameter_disable_active_migration:
            if (!value.empty()) {
                error = codec_error("disable_active_migration transport parameter must be empty");
            } else {
                result.disable_active_migration = true;
            }
            break;
        case transport_parameter_active_connection_id_limit:
            error = detail::assign_numeric_parameter(result.active_connection_id_limit, value, "malformed active_connection_id_limit transport parameter");
            if (error.ok() && *result.active_connection_id_limit < 2) {
                error = codec_error("active_connection_id_limit transport parameter must be at least 2");
            }
            break;
        default:
            result.unknown.push_back(unknown_transport_parameter{id, flowq::buffer{value}});
            break;
        }
        if (!error.ok()) {
            return {{}, error};
        }
    }
    return {std::move(result), {}};
}

[[nodiscard]] inline transport_parameters_decode_result decode_transport_parameters(const flowq::buffer& input) {
    return decode_transport_parameters(std::span<const std::byte>{input.data(), input.size()});
}

} // namespace flowq::quic
