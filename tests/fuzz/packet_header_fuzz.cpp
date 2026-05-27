#include <flowq/quic/packet_header.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> input{
        reinterpret_cast<const std::byte*>(data), size};

    auto result = flowq::quic::decode_packet_header(input);
    (void)result;

    return 0;
}
