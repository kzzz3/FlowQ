#include <flowq/quic/packet_pipeline.hpp>

#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <vector>

namespace {

std::vector<std::byte> bytes(std::initializer_list<unsigned int> values) {
    std::vector<std::byte> output;
    output.reserve(values.size());
    for (auto value : values) {
        output.push_back(static_cast<std::byte>(value & 0xffU));
    }
    return output;
}

flowq::quic::connection_id cid(std::initializer_list<unsigned int> values) {
    return flowq::quic::connection_id{flowq::buffer{bytes(values)}};
}

flowq::quic::packet_build_request make_request(
    const flowq::quic::packet_protector& protector,
    flowq::quic::packet_protection_policy policy) {
    return flowq::quic::packet_build_request{
        flowq::quic::long_packet_type::initial,
        1,
        cid({0x01}),
        cid({0x02}),
        {},
        flowq::quic::packet_number{flowq::quic::packet_number_space::initial, 0},
        {flowq::quic::frame{flowq::quic::ping_frame{}}},
        &protector,
        flowq::quic::packet_pipeline_config{1200},
        policy
    };
}

} // namespace

int main() {
    // Plaintext protection is test-only. Production-required policy rejects it.
    flowq::quic::plaintext_packet_protector protector{};

    auto allowed = flowq::quic::assemble_long_packet(make_request(
        protector,
        flowq::quic::packet_protection_policy::test_allowed));
    if (!allowed.ok()) {
        std::cerr << "test_allowed unexpectedly rejected plaintext protection\n";
        return 1;
    }

    auto rejected = flowq::quic::assemble_long_packet(make_request(
        protector,
        flowq::quic::packet_protection_policy::production_required));
    if (rejected.ok()) {
        std::cerr << "production_required unexpectedly accepted plaintext protection\n";
        return 1;
    }

    std::cout << "FlowQ packet policy example: test_allowed accepts plaintext, production_required rejects it\n";
    return 0;
}
