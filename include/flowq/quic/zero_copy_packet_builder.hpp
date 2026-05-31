#pragma once

#include <flowq/buffer.hpp>
#include <flowq/quic/packet_pipeline.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace flowq::quic {

/// Zero-copy packet builder that writes directly into a pre-allocated buffer.
///
/// Instead of creating intermediate vectors for frame bytes, AAD, protected payload,
/// and final datagram, this builder writes directly into a single pre-allocated buffer.
///
/// Reduces copies from 6 to 1 (only the final header protection XOR).
class zero_copy_packet_builder {
public:
    /// Create a builder with pre-allocated capacity.
    explicit zero_copy_packet_builder(std::size_t max_datagram_size)
        : buffer_(max_datagram_size) {}

    /// Build a long packet (Initial or Handshake) with minimal copies.
    [[nodiscard]] assembled_packet build_long_packet(const packet_build_request& request) {
        if (request.protector == nullptr) {
            return {{}, request.number, protection_level::none, detail::pipeline_error("packet protector is required")};
        }

        // Reset write position
        write_pos_ = 0;

        // Reserve space for header (will be filled later)
        // Initial header max: 1 (type) + 4 (version) + 1 (dcid_len) + 20 (dcid) + 1 (scid_len) + 20 (scid) + 1 (token_len) + token + 2 (length) = ~50
        constexpr std::size_t max_header_reserve = 64;
        write_pos_ += max_header_reserve;

        // Write frame bytes directly into buffer
        const auto frame_start = write_pos_;
        for (const auto& frame : request.frames) {
            const auto encoded = std::visit(
                [](const auto& concrete_frame) {
                    return encode_frame(concrete_frame);
                },
                frame);
            if (!encoded.ok()) {
                return {{}, request.number, request.protector->level(), encoded.error};
            }
            write_span(encoded.payload);
        }
        const auto frame_size = write_pos_ - frame_start;

        // Add padding if needed for Initial packets
        if (request.enforce_initial_datagram_minimum && request.type == long_packet_type::initial) {
            const auto current_size = max_header_reserve + detail::fixed_packet_number_length + frame_size + request.protector->protection_overhead();
            if (current_size < detail::minimum_client_initial_datagram_size) {
                const auto padding = detail::minimum_client_initial_datagram_size - current_size;
                std::memset(buffer_.data() + write_pos_, 0, padding);
                write_pos_ += padding;
            }
        }

        // Calculate protected payload length
        const auto protected_payload_length = detail::fixed_packet_number_length + frame_size + request.protector->protection_overhead();

        // Build header at the beginning of buffer
        std::size_t header_pos = 0;
        auto write_byte = [&](std::byte b) { buffer_[header_pos++] = b; };
        auto write_u32 = [&](std::uint32_t v) {
            buffer_[header_pos++] = static_cast<std::byte>((v >> 24) & 0xff);
            buffer_[header_pos++] = static_cast<std::byte>((v >> 16) & 0xff);
            buffer_[header_pos++] = static_cast<std::byte>((v >> 8) & 0xff);
            buffer_[header_pos++] = static_cast<std::byte>(v & 0xff);
        };
        auto write_cid = [&](const connection_id& cid) {
            buffer_[header_pos++] = static_cast<std::byte>(cid.bytes.size());
            std::memcpy(buffer_.data() + header_pos, cid.bytes.data(), cid.bytes.size());
            header_pos += cid.bytes.size();
        };
        auto write_varint = [&](std::uint64_t v) {
            if (v < 0x40) {
                buffer_[header_pos++] = static_cast<std::byte>(v);
            } else if (v < 0x4000) {
                buffer_[header_pos++] = static_cast<std::byte>(0x40 | (v >> 8));
                buffer_[header_pos++] = static_cast<std::byte>(v & 0xff);
            } else if (v < 0x40000000) {
                buffer_[header_pos++] = static_cast<std::byte>(0x80 | (v >> 24));
                buffer_[header_pos++] = static_cast<std::byte>((v >> 16) & 0xff);
                buffer_[header_pos++] = static_cast<std::byte>((v >> 8) & 0xff);
                buffer_[header_pos++] = static_cast<std::byte>(v & 0xff);
            }
        };

        // First byte
        write_byte(detail::long_header_first_byte(request.type));

        // Version
        write_u32(request.version);

        // DCID
        write_cid(request.destination_connection_id);

        // SCID
        write_cid(request.source_connection_id);

        // Token (Initial only)
        if (request.type == long_packet_type::initial) {
            write_varint(request.token.size());
            if (!request.token.empty()) {
                std::memcpy(buffer_.data() + header_pos, request.token.data(), request.token.size());
                header_pos += request.token.size();
            }
        }

        // Length
        write_varint(protected_payload_length);

        // Packet number (4 bytes, will be XORed with header protection mask)
        const auto pn_pos = header_pos;
        buffer_[header_pos++] = static_cast<std::byte>((request.number.value >> 24) & 0xff);
        buffer_[header_pos++] = static_cast<std::byte>((request.number.value >> 16) & 0xff);
        buffer_[header_pos++] = static_cast<std::byte>((request.number.value >> 8) & 0xff);
        buffer_[header_pos++] = static_cast<std::byte>(request.number.value & 0xff);

        // Move frame bytes to after header (if needed due to reserve)
        const auto actual_header_size = header_pos;
        if (max_header_reserve != actual_header_size) {
            const auto frame_data = buffer_.data() + max_header_reserve;
            const auto new_frame_pos = buffer_.data() + actual_header_size;
            std::memmove(new_frame_pos, frame_data, frame_size);
        }

        // Build AAD for AEAD (header prefix + packet number)
        std::vector<std::byte> aad(actual_header_size - detail::fixed_packet_number_length + detail::fixed_packet_number_length);
        std::memcpy(aad.data(), buffer_.data(), actual_header_size);

        // Encrypt frame bytes in-place
        const auto plaintext_start = actual_header_size;
        std::span<const std::byte> plaintext{buffer_.data() + plaintext_start, frame_size};
        auto protected_result = detail::protect_payload(
            *request.protector,
            packet_protection_context{request.number, std::span<const std::byte>{aad.data(), aad.size()}},
            std::vector<std::byte>{plaintext.begin(), plaintext.end()});

        if (!protected_result.ok()) {
            return {{}, request.number, request.protector->level(), protected_result.error};
        }

        // Write protected payload (packet number + encrypted frames + tag)
        const auto total_size = actual_header_size + protected_result.payload.size();
        if (total_size > buffer_.size()) {
            return {{}, request.number, request.protector->level(), detail::pipeline_error("datagram exceeds max size")};
        }

        // Copy protected payload after header (overwriting plaintext frames)
        std::memcpy(buffer_.data() + actual_header_size, protected_result.payload.data(), protected_result.payload.size());

        // Apply header protection
        if (request.protector->header_protection_enabled()) {
            if (auto error = detail::apply_long_header_protection(
                    std::span<std::byte>{buffer_.data(), total_size},
                    protected_payload_length,
                    *request.protector); !error.ok()) {
                return {{}, request.number, request.protector->level(), error};
            }
        }

        // Create result buffer
        flowq::buffer result{std::vector<std::byte>{buffer_.data(), buffer_.data() + total_size}};
        return {std::move(result), request.number, request.protector->level(), {}};
    }

private:
    std::vector<std::byte> buffer_;
    std::size_t write_pos_{0};

    void write_span(std::span<const std::byte> data) {
        std::memcpy(buffer_.data() + write_pos_, data.data(), data.size());
        write_pos_ += data.size();
    }
};

/// Buffer pool for reusing datagram buffers across packets.
///
/// Avoids repeated allocation/deallocation of datagram-sized buffers.
class datagram_buffer_pool {
public:
    explicit datagram_buffer_pool(std::size_t buffer_size, std::size_t pool_size = 16)
        : buffer_size_{buffer_size} {
        pool_.reserve(pool_size);
        for (std::size_t i = 0; i < pool_size; ++i) {
            pool_.emplace_back(buffer_size);
        }
    }

    /// Acquire a buffer from the pool.
    [[nodiscard]] std::vector<std::byte> acquire() {
        if (!pool_.empty()) {
            auto buf = std::move(pool_.back());
            pool_.pop_back();
            return buf;
        }
        return std::vector<std::byte>(buffer_size_);
    }

    /// Release a buffer back to the pool.
    void release(std::vector<std::byte>&& buf) {
        if (pool_.size() < max_pool_size_) {
            buf.clear();
            pool_.push_back(std::move(buf));
        }
    }

    /// Get current pool size.
    [[nodiscard]] std::size_t size() const noexcept { return pool_.size(); }

private:
    std::size_t buffer_size_;
    std::vector<std::vector<std::byte>> pool_;
    static constexpr std::size_t max_pool_size_ = 32;
};

} // namespace flowq::quic
