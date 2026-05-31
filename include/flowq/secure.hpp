#pragma once

#include <cstddef>
#include <cstring>
#include <span>
#include <type_traits>
#include <vector>

// Platform-specific secure zero functions
#if defined(_WIN32)
// Use Windows API with minimal includes
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <string.h>
#elif defined(__linux__)
#include <string.h>
#endif

namespace flowq {

/// Securely zero memory to prevent sensitive data from lingering in memory.
/// Uses platform-specific functions that are guaranteed not to be optimized away.
///
/// Windows: SecureZeroMemory()
/// Linux/macOS: explicit_bzero() or OPENSSL_cleanse() if available
/// Fallback: volatile write barrier
inline void secure_zero(void* ptr, std::size_t size) noexcept {
    if (ptr == nullptr || size == 0) {
        return;
    }

#if defined(_WIN32)
    // Windows: SecureZeroMemory is guaranteed not to be optimized away
    SecureZeroMemory(ptr, size);
#elif defined(__APPLE__)
    // macOS: memset_s is the secure alternative
    memset_s(ptr, size, 0, size);
#elif defined(__linux__)
    // Linux: explicit_bzero is available in glibc 2.25+
    // Fallback to volatile write if not available
    #if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 25))
        explicit_bzero(ptr, size);
    #else
        // Volatile write barrier - compiler cannot optimize this away
        volatile unsigned char* p = static_cast<volatile unsigned char*>(ptr);
        while (size--) {
            *p++ = 0;
        }
    #endif
#else
    // Fallback: volatile write barrier
    volatile unsigned char* p = static_cast<volatile unsigned char*>(ptr);
    while (size--) {
        *p++ = 0;
    }
#endif
}

/// Securely zero a span of bytes.
inline void secure_zero(std::span<std::byte> data) noexcept {
    secure_zero(data.data(), data.size());
}

/// Securely zero a vector of bytes and clear it.
inline void secure_zero_and_clear(std::vector<std::byte>& data) noexcept {
    secure_zero(data.data(), data.size());
    data.clear();
}

/// Securely zero a buffer type that has data() and size() methods.
template <typename T>
    requires requires(const T& t) {
        { t.data() } -> std::convertible_to<const void*>;
        { t.size() } -> std::convertible_to<std::size_t>;
    }
inline void secure_zero_buffer(T& buffer) noexcept {
    secure_zero(const_cast<void*>(static_cast<const void*>(buffer.data())), buffer.size());
}

/// RAII wrapper for sensitive data that automatically zeroes on destruction.
template <typename T>
class secure_storage {
public:
    secure_storage() = default;

    explicit secure_storage(std::vector<T> data) noexcept
        : data_{std::move(data)} {}

    ~secure_storage() {
        secure_zero();
    }

    // Move semantics
    secure_storage(secure_storage&& other) noexcept
        : data_{std::move(other.data_)} {
        // Other's data is now empty, no need to zero
    }

    secure_storage& operator=(secure_storage&& other) noexcept {
        if (this != &other) {
            secure_zero();
            data_ = std::move(other.data_);
        }
        return *this;
    }

    // Delete copy semantics for security
    secure_storage(const secure_storage&) = delete;
    secure_storage& operator=(const secure_storage&) = delete;

    /// Access the underlying data (const).
    [[nodiscard]] const std::vector<T>& data() const noexcept {
        return data_;
    }

    /// Access the underlying data (mutable).
    [[nodiscard]] std::vector<T>& mutable_data() noexcept {
        return data_;
    }

    /// Get a span view of the data.
    [[nodiscard]] std::span<const T> span() const noexcept {
        return std::span<const T>{data_.data(), data_.size()};
    }

    /// Get a mutable span view of the data.
    [[nodiscard]] std::span<T> mutable_span() noexcept {
        return std::span<T>{data_.data(), data_.size()};
    }

    /// Check if the storage is empty.
    [[nodiscard]] bool empty() const noexcept {
        return data_.empty();
    }

    /// Get the size of the data.
    [[nodiscard]] std::size_t size() const noexcept {
        return data_.size();
    }

    /// Explicitly zero the data.
    void secure_zero() noexcept {
        if (!data_.empty()) {
            flowq::secure_zero(data_.data(), data_.size() * sizeof(T));
            data_.clear();
        }
    }

private:
    std::vector<T> data_;
};

/// Type alias for secure byte storage.
using secure_bytes = secure_storage<std::byte>;

} // namespace flowq
