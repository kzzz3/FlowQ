#pragma once

#include <cstddef>
#include <ranges>
#include <span>
#include <type_traits>
#include <vector>

namespace flowq {

class buffer {
public:
    using value_type = std::byte;

    buffer() = default;

    explicit buffer(std::span<const std::byte> bytes)
        : bytes_{bytes.begin(), bytes.end()} {}

    template <std::ranges::contiguous_range Range>
        requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<Range>>, std::byte>
    explicit buffer(const Range& bytes)
        : buffer{std::span<const std::byte>{std::ranges::data(bytes), std::ranges::size(bytes)}} {}

    [[nodiscard]] const std::byte* data() const noexcept {
        return bytes_.data();
    }

    [[nodiscard]] std::byte* data() noexcept {
        return bytes_.data();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return bytes_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return bytes_.empty();
    }

private:
    std::vector<std::byte> bytes_{};
};

} // namespace flowq
