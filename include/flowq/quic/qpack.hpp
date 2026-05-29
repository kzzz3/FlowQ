#pragma once

#include <flowq/buffer.hpp>
#include <flowq/error.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace flowq::quic::qpack {

/// QPACK static table entry (RFC 9204 §3.2).
struct static_table_entry {
    std::string name;
    std::string value;
};

/// QPACK header field representation.
struct header_field {
    std::string name;
    std::string value;
};

/// QPACK encoder error.
[[nodiscard]] inline flowq::error qpack_error(const char* message) {
    return flowq::error{flowq::error_code::protocol_error, message};
}

/// QPACK static table (RFC 9204 Appendix A).
/// Returns the static table entries for common HTTP headers.
[[nodiscard]] inline const std::vector<static_table_entry>& static_table() {
    static const std::vector<static_table_entry> table = {
        {":authority", ""},
        {":path", "/"},
        {"age", "0"},
        {"content-disposition", ""},
        {"content-length", "0"},
        {"cookie", ""},
        {"date", ""},
        {"etag", ""},
        {"if-modified-since", ""},
        {"if-none-match", ""},
        {"last-modified", ""},
        {"link", ""},
        {"location", ""},
        {"referer", ""},
        {"set-cookie", ""},
        {":method", "CONNECT"},
        {":method", "DELETE"},
        {":method", "GET"},
        {":method", "HEAD"},
        {":method", "OPTIONS"},
        {":method", "POST"},
        {":method", "PUT"},
        {":scheme", "http"},
        {":scheme", "https"},
        {":status", "100"},
        {":status", "103"},
        {":status", "200"},
        {":status", "204"},
        {":status", "206"},
        {":status", "302"},
        {":status", "304"},
        {":status", "400"},
        {":status", "404"},
        {":status", "503"},
        {"accept", "*/*"},
        {"accept", "application/dns-message"},
        {"accept-encoding", "gzip, deflate, br"},
        {"accept-ranges", "bytes"},
        {"access-control-allow-headers", "cache-control"},
        {"access-control-allow-headers", "content-type"},
        {"access-control-allow-origin", "*"},
        {"cache-control", "max-age=0"},
        {"cache-control", "max-age=2592000"},
        {"cache-control", "max-age=604800"},
        {"cache-control", "no-cache"},
        {"cache-control", "no-store"},
        {"cache-control", "public, max-age=31536000"},
        {"content-encoding", "br"},
        {"content-encoding", "gzip"},
        {"content-type", "application/dns-message"},
        {"content-type", "application/javascript"},
        {"content-type", "application/json"},
        {"content-type", "application/x-www-form-urlencoded"},
        {"content-type", "image/gif"},
        {"content-type", "image/jpeg"},
        {"content-type", "image/png"},
        {"content-type", "text/css"},
        {"content-type", "text/html; charset=utf-8"},
        {"content-type", "text/plain"},
        {"content-type", "text/plain;charset=utf-8"},
        {"range", "bytes=0-"},
        {"strict-transport-security", "max-age=31536000"},
        {"strict-transport-security", "max-age=31536000; includesubdomains"},
        {"strict-transport-security", "max-age=31536000; includesubdomains; preload"},
        {"vary", "accept-encoding"},
        {"vary", "origin"},
        {"x-content-type-options", "nosniff"},
        {"x-xss-protection", "1; mode=block"},
        {"dnt", "1"},
        {"x-frame-options", "deny"},
    };
    return table;
}

/// QPACK dynamic table for encoding.
/// Maintains a dynamic table of recently encoded headers.
class dynamic_table {
public:
    explicit dynamic_table(std::uint64_t max_capacity = 4096)
        : max_capacity_{max_capacity} {}

    /// Add an entry to the dynamic table.
    void add(const std::string& name, const std::string& value) {
        auto size = entry_size(name, value);
        
        // Evict entries if needed
        while (capacity_ + size > max_capacity_ && !entries_.empty()) {
            capacity_ -= entry_size(entries_.front().name, entries_.front().value);
            entries_.erase(entries_.begin());
        }
        
        if (size <= max_capacity_) {
            entries_.push_back({name, value});
            capacity_ += size;
        }
    }

    /// Find an entry in the dynamic table.
    [[nodiscard]] std::optional<std::uint64_t> find(const std::string& name, const std::string& value) const {
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].name == name && entries_[i].value == value) {
                return static_cast<std::uint64_t>(i);
            }
        }
        return std::nullopt;
    }

    /// Find an entry by name only.
    [[nodiscard]] std::optional<std::uint64_t> find_name(const std::string& name) const {
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].name == name) {
                return static_cast<std::uint64_t>(i);
            }
        }
        return std::nullopt;
    }

    /// Get current capacity usage.
    [[nodiscard]] std::uint64_t capacity() const noexcept {
        return capacity_;
    }

    /// Get maximum capacity.
    [[nodiscard]] std::uint64_t max_capacity() const noexcept {
        return max_capacity_;
    }

    /// Get number of entries.
    [[nodiscard]] std::size_t size() const noexcept {
        return entries_.size();
    }

private:
    std::vector<header_field> entries_;
    std::uint64_t capacity_{};
    std::uint64_t max_capacity_;

    [[nodiscard]] static std::uint64_t entry_size(const std::string& name, const std::string& value) {
        return name.size() + value.size() + 32;
    }
};

/// QPACK encoder result.
struct encode_result {
    flowq::buffer data;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

/// QPACK decoder result.
struct decode_result {
    std::vector<header_field> headers;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};

/// QPACK encoder.
/// Encodes HTTP/3 headers using QPACK static and dynamic tables.
class encoder {
public:
    explicit encoder(std::uint64_t max_dynamic_table_capacity = 4096)
        : dynamic_table_{max_dynamic_table_capacity} {
        // Build static table lookup cache
        build_static_cache();
    }

    /// Encode a list of header fields.
    [[nodiscard]] encode_result encode(const std::vector<header_field>& headers) {
        std::vector<std::byte> body;
        
        // Track the base index (total dynamic table entries referenced or inserted)
        std::uint64_t base_index = 0;
        std::uint64_t required_insert_count = 0;
        
        for (const auto& header : headers) {
            // Try static table first (fast path)
            auto static_index = find_in_static_table_fast(header.name, header.value);
            if (static_index.has_value()) {
                // Static table reference with value
                encode_static_reference(body, *static_index);
                continue;
            }
            
            // Try dynamic table
            auto dynamic_index = dynamic_table_.find(header.name, header.value);
            if (dynamic_index.has_value()) {
                // Dynamic table reference — track the referenced index
                encode_dynamic_reference(body, *dynamic_index);
                if (*dynamic_index >= base_index) {
                    base_index = *dynamic_index + 1;
                }
                continue;
            }
            
            // Try static table name only (fast path)
            auto static_name_index = find_static_name_fast(header.name);
            if (static_name_index.has_value()) {
                // Static name reference + literal value
                encode_static_name_with_literal_value(body, *static_name_index, header.value);
                // Insert into dynamic table
                dynamic_table_.add(header.name, header.value);
                ++required_insert_count;
                base_index = required_insert_count;
                continue;
            }
            
            // Try dynamic table name only
            auto dynamic_name_index = dynamic_table_.find_name(header.name);
            if (dynamic_name_index.has_value()) {
                // Dynamic name reference + literal value
                encode_dynamic_name_with_literal_value(body, *dynamic_name_index, header.value);
                // Insert into dynamic table
                dynamic_table_.add(header.name, header.value);
                ++required_insert_count;
                base_index = required_insert_count;
                continue;
            }
            
            // Literal name and value
            encode_literal_header(body, header.name, header.value);
            // Insert into dynamic table
            dynamic_table_.add(header.name, header.value);
            ++required_insert_count;
            base_index = required_insert_count;
        }
        
        // Build output with prefix (RIC + Delta Base) followed by body
        std::vector<std::byte> output;
        
        // Encode Required Insert Count
        if (required_insert_count == 0) {
            output.push_back(std::byte{0x00});
        } else {
            encode_varint_to(output, required_insert_count);
        }
        
        // Encode Delta Base: difference between base index and required insert count.
        // Per RFC 9204 §4.5.2:
        //   T=0 (sign bit): Base = Required Insert Count + Delta Base
        //   T=1 (sign bit): Base = Required Insert Count - Delta Base
        // Since base_index >= required_insert_count in our encoding, use T=0.
        std::uint64_t delta = base_index - required_insert_count;
        // Encode sign bit (0 for positive) + 7-bit delta value
        std::uint8_t delta_base_byte = static_cast<std::uint8_t>(delta & 0x7f);
        output.push_back(static_cast<std::byte>(delta_base_byte));
        
        // Append encoded body
        output.insert(output.end(), body.begin(), body.end());
        
        return {flowq::buffer{output}, {}};
    }

private:
    dynamic_table dynamic_table_;

    // Static table lookup cache for fast name+value matching
    struct static_cache_entry {
        std::uint64_t index{};
        bool has_value_match{};
    };

    // Cache for name+value -> index lookup
    std::unordered_map<std::string, std::unordered_map<std::string, std::uint64_t>> static_name_value_cache_;

    // Cache for name-only -> first index lookup
    std::unordered_map<std::string, std::uint64_t> static_name_cache_;

    void build_static_cache() {
        const auto& table = static_table();
        for (std::size_t i = 0; i < table.size(); ++i) {
            const auto& entry = table[i];

            // Cache name+value pair
            auto& value_map = static_name_value_cache_[entry.name];
            if (value_map.find(entry.value) == value_map.end()) {
                value_map[entry.value] = static_cast<std::uint64_t>(i);
            }

            // Cache name-only (first occurrence)
            if (static_name_cache_.find(entry.name) == static_name_cache_.end()) {
                static_name_cache_[entry.name] = static_cast<std::uint64_t>(i);
            }
        }
    }

    [[nodiscard]] std::optional<std::uint64_t> find_in_static_table_fast(const std::string& name, const std::string& value) const {
        auto name_it = static_name_value_cache_.find(name);
        if (name_it != static_name_value_cache_.end()) {
            auto value_it = name_it->second.find(value);
            if (value_it != name_it->second.end()) {
                return value_it->second;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint64_t> find_static_name_fast(const std::string& name) const {
        auto it = static_name_cache_.find(name);
        if (it != static_name_cache_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::uint64_t> find_in_static_table(const std::string& name, const std::string& value) const {
        return find_in_static_table_fast(name, value);
    }

    [[nodiscard]] std::optional<std::uint64_t> find_static_name(const std::string& name) const {
        return find_static_name_fast(name);
    }

    static void encode_static_reference(std::vector<std::byte>& output, std::uint64_t index) {
        // Encode as Static Indexed Header Field (0b10000000 + 7-bit index)
        if (index < 128) {
            output.push_back(static_cast<std::byte>(0x80 | index));
        } else {
            // Multi-byte encoding for larger indices
            output.push_back(std::byte{0xff});
            encode_varint_to(output, index - 128);
        }
    }

    static void encode_dynamic_reference(std::vector<std::byte>& output, std::uint64_t index) {
        // Encode as Dynamic Indexed Header Field (0b10000000 with different prefix)
        // For simplicity, use same encoding as static but with different prefix bit
        if (index < 128) {
            output.push_back(static_cast<std::byte>(0x80 | index));
        } else {
            output.push_back(std::byte{0xff});
            encode_varint_to(output, index - 128);
        }
    }

    static void encode_static_name_with_literal_value(std::vector<std::byte>& output, std::uint64_t name_index, const std::string& value) {
        // Encode as Literal Header Field with Static Name Reference (0b01000000 + 6-bit index)
        if (name_index < 64) {
            output.push_back(static_cast<std::byte>(0x40 | name_index));
        } else {
            output.push_back(std::byte{0x7f});
            encode_varint_to(output, name_index - 64);
        }
        encode_string(output, value);
    }

    static void encode_dynamic_name_with_literal_value(std::vector<std::byte>& output, std::uint64_t name_index, const std::string& value) {
        // Encode as Literal Header Field with Dynamic Name Reference
        if (name_index < 64) {
            output.push_back(static_cast<std::byte>(0x40 | name_index));
        } else {
            output.push_back(std::byte{0x7f});
            encode_varint_to(output, name_index - 64);
        }
        encode_string(output, value);
    }

    static void encode_literal_header(std::vector<std::byte>& output, const std::string& name, const std::string& value) {
        // Encode as Literal Header Field without Name Reference (0b00100000)
        output.push_back(std::byte{0x20});
        encode_string(output, name);
        encode_string(output, value);
    }

    static void encode_string(std::vector<std::byte>& output, const std::string& str) {
        // Encode string length as varint
        encode_varint_to(output, str.size());
        // Encode string bytes
        for (char c : str) {
            output.push_back(static_cast<std::byte>(c));
        }
    }

    static void encode_varint_to(std::vector<std::byte>& output, std::uint64_t value) {
        // LEB128-style encoding: high bit set on all bytes except the last
        while (value >= 0x80) {
            output.push_back(static_cast<std::byte>(0x80 | (value & 0x7f)));
            value >>= 7;
        }
        output.push_back(static_cast<std::byte>(value));
    }
};

/// QPACK decoder.
/// Decodes QPACK-encoded HTTP/3 headers.
class decoder {
public:
    /// Decode QPACK-encoded headers.
    [[nodiscard]] decode_result decode(const std::byte* data, std::size_t size) {
        if (size < 2) {
            return {{}, qpack_error("QPACK data too short")};
        }

        std::size_t offset = 0;
        
        // Required Insert Count
        auto required_insert_count = data[offset++];
        
        // Delta Base
        auto delta_base = data[offset++];
        
        std::vector<header_field> headers;
        
        while (offset < size) {
            auto byte = static_cast<std::uint8_t>(data[offset]);
            
            if (byte & 0x80) {
                // Static Indexed Header Field
                auto index = byte & 0x7f;
                if (index >= static_table().size()) {
                    return {{}, qpack_error("Invalid static table index")};
                }
                headers.push_back({static_table()[index].name, static_table()[index].value});
                ++offset;
            } else if (byte & 0x40) {
                // Literal Header Field with Static Name Reference
                auto name_index = byte & 0x3f;
                ++offset;
                if (name_index >= static_table().size()) {
                    return {{}, qpack_error("Invalid static name index")};
                }
                auto value = decode_string(data, size, offset);
                if (!value.has_value()) {
                    return {{}, qpack_error("Failed to decode string")};
                }
                headers.push_back({static_table()[name_index].name, *value});
            } else if (byte & 0x20) {
                // Literal Header Field without Name Reference
                ++offset;
                auto name = decode_string(data, size, offset);
                if (!name.has_value()) {
                    return {{}, qpack_error("Failed to decode name")};
                }
                auto value = decode_string(data, size, offset);
                if (!value.has_value()) {
                    return {{}, qpack_error("Failed to decode value")};
                }
                headers.push_back({*name, *value});
            } else {
                return {{}, qpack_error("Unknown QPACK instruction")};
            }
        }
        
        return {headers, {}};
    }

private:
    [[nodiscard]] static std::optional<std::string> decode_string(const std::byte* data, std::size_t size, std::size_t& offset) {
        if (offset >= size) {
            return std::nullopt;
        }
        
        // Decode length using LEB128-style variable-length integer.
        // High bit (0x80) of each byte is the continuation flag:
        //   1 = more bytes follow, 0 = final byte.
        // Bits 6-0 of each byte contribute 7 bits to the integer value
        // (least significant group first).
        std::uint64_t length = 0;
        int shift = 0;
        while (offset < size) {
            auto byte = static_cast<std::uint8_t>(data[offset++]);
            length |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
            if (!(byte & 0x80)) {
                break;  // Final byte (continuation bit clear)
            }
            shift += 7;
        }
        
        if (offset + length > size) {
            return std::nullopt;
        }
        
        std::string result;
        result.reserve(length);
        for (std::size_t i = 0; i < length; ++i) {
            result.push_back(static_cast<char>(data[offset + i]));
        }
        offset += length;
        
        return result;
    }
};

} // namespace flowq::quic::qpack
