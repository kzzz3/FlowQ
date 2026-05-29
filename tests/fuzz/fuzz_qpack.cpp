#include "quic_codec_fuzz_harness.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> input{
        reinterpret_cast<const std::byte*>(data), size};

    flowq::quic::test_fuzz::exercise_qpack_input(input);

    return 0;
}
