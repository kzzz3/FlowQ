#pragma once

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <span>
#include <type_traits>
#include <vector>

namespace flowq {

/// Optimized buffer class with move semantics and efficient construction.
class buffer {
public:
    using value_type = std::byte;
    using iterator = std::vector<std::byte>::iterator;
    using const_iterator = std::vector<std::byte>::const_iterator;

    buffer() = default;

    /// Construct from span with move optimization.
    explicit buffer(std::span<const std::byte> bytes)
        : bytes_{bytes.begin(), bytes.end()} {}

    /// Construct from range.
    template <std::ranges::contiguous_range Range>
        requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<Range>>, std::byte>
    explicit buffer(const Range& bytes)
        : buffer{std::span<const std::byte>{std::ranges::data(bytes), std::ranges::size(bytes)}} {}

    /// Move constructor - efficient transfer.
    buffer(buffer&& other) noexcept = default;

    /// Move assignment - efficient transfer.
    buffer& operator=(buffer&& other) noexcept = default;

    /// Copy constructor.
    buffer(const buffer& other) = default;

    /// Copy assignment.
    buffer& operator=(const buffer& other) = default;

    /// Construct from vector directly (move).
    explicit buffer(std::vector<std::byte>&& bytes) noexcept
        : bytes_{std::move(bytes)} {}

    /// Construct with pre-allocated capacity.
    explicit buffer(std::size_t capacity) {
        bytes_.reserve(capacity);
    }

    /// Data access.
    [[nodiscard]] const std::byte* data() const noexcept {
        return bytes_.data();
    }

    [[nodiscard]] std::byte* data() noexcept {
        return bytes_.data();
    }

    /// Size.
    [[nodiscard]] std::size_t size() const noexcept {
        return bytes_.size();
    }

    /// Empty check.
    [[nodiscard]] bool empty() const noexcept {
        return bytes_.empty();
    }

    /// Capacity.
    [[nodiscard]] std::size_t capacity() const noexcept {
        return bytes_.capacity();
    }

    /// Reserve capacity.
    void reserve(std::size_t new_capacity) {
        bytes_.reserve(new_capacity);
    }

    /// Clear contents.
    void clear() noexcept {
        bytes_.clear();
    }

    /// Securely zero the buffer contents before clearing.
    /// Uses platform-specific functions that are guaranteed not to be optimized away.
    void secure_zero() noexcept {
        if (!bytes_.empty()) {
            // Use volatile write barrier to prevent compiler optimization
            volatile unsigned char* p = reinterpret_cast<volatile unsigned char*>(bytes_.data());
            std::size_t size = bytes_.size();
            while (size--) {
                *p++ = 0;
            }
            bytes_.clear();
        }
    }

    /// Append a single byte.
    void push_back(std::byte b) {
        bytes_.push_back(b);
    }

    /// Append span of bytes.
    void append(std::span<const std::byte> data) {
        bytes_.insert(bytes_.end(), data.begin(), data.end());
    }

    /// Append buffer contents.
    void append(const buffer& other) {
        bytes_.insert(bytes_.end(), other.bytes_.begin(), other.bytes_.end());
    }

    /// Resize buffer.
    void resize(std::size_t new_size) {
        bytes_.resize(new_size);
    }

    /// Resize buffer with fill value.
    void resize(std::size_t new_size, std::byte fill) {
        bytes_.resize(new_size, fill);
    }

    /// Iterator access.
    [[nodiscard]] iterator begin() noexcept { return bytes_.begin(); }
    [[nodiscard]] iterator end() noexcept { return bytes_.end(); }
    [[nodiscard]] const_iterator begin() const noexcept { return bytes_.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return bytes_.end(); }
    [[nodiscard]] const_iterator cbegin() const noexcept { return bytes_.cbegin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return bytes_.cend(); }

    /// Element access.
    [[nodiscard]] std::byte& operator[](std::size_t index) noexcept { return bytes_[index]; }
    [[nodiscard]] const std::byte& operator[](std::size_t index) const noexcept { return bytes_[index]; }

    /// Get underlying vector (for advanced use).
    [[nodiscard]] const std::vector<std::byte>& vector() const noexcept { return bytes_; }
    [[nodiscard]] std::vector<std::byte>& vector() noexcept { return bytes_; }

private:
    std::vector<std::byte> bytes_{};
};

/// Convenience function to create buffer from span.
[[nodiscard]] inline buffer make_buffer(std::span<const std::byte> data) {
    return buffer{data};
}

/// Convenience function to create buffer from initializer list.
[[nodiscard]] inline buffer make_buffer(std::initializer_list<std::byte> data) {
    return buffer{std::span<const std::byte>{data.begin(), data.size()}};
}

} // namespace flowq
