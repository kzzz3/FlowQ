#pragma once

#include <cstdint>
#include <string>

namespace flowq {

struct endpoint {
    std::string host;
    std::uint16_t port{};
    std::string alpn;
};

} // namespace flowq
