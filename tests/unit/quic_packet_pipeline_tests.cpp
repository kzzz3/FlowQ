#include <flowq/quic/packet_pipeline.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <initializer_list>
#include <span>
#include <variant>
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

void check_buffer(const flowq::buffer& buffer, std::initializer_list<unsigned int> expected) {
    REQUIRE(buffer.size() == expected.size());
    std::size_t index = 0;
    for (auto value : expected) {
        CHECK(buffer.data()[index] == static_cast<std::byte>(value & 0xffU));
        ++index;
    }
}

class xor_packet_protector final : public flowq::quic::packet_protector {
public:
    explicit xor_packet_protector(flowq::quic::protection_level level = flowq::quic::protection_level::initial) : level_{level} {}

    flowq::quic::protection_level level() const noexcept override {
        return level_;
    }

    flowq::quic::packet_protection_result protect(std::span<const std::byte> plaintext) const override {
        ++protect_calls;
        return {transform(plaintext), {}};
    }

    flowq::quic::packet_protection_result unprotect(std::span<const std::byte> protected_payload) const override {
        ++unprotect_calls;
        return {transform(protected_payload), {}};
    }

    mutable int protect_calls{};
    mutable int unprotect_calls{};

private:
    flowq::quic::protection_level level_{};

    static flowq::buffer transform(std::span<const std::byte> input) {
        std::vector<std::byte> output;
        output.reserve(input.size());
        for (auto byte : input) {
            output.push_back(byte ^ std::byte{0xa5});
        }
        return flowq::buffer{output};
    }
};

flowq::quic::connection_id cid(std::initializer_list<unsigned int> values) {
    return flowq::quic::connection_id{flowq::buffer{bytes(values)}};
}

TEST_CASE("packet pipeline rejects invalid packet metadata invariants") {
    flowq::quic::plaintext_packet_protector plaintext{};
    flowq::quic::packet_build_request request{
        flowq::quic::long_packet_type::initial,
        1,
        cid({0xaa}),
        cid({0xbb}),
        {},
        flowq::quic::packet_number{flowq::quic::packet_number_space::initial, 0x1'0000'0000ULL},
        {flowq::quic::frame{flowq::quic::ping_frame{}}},
        &plaintext,
        flowq::quic::packet_pipeline_config{1200}
    };

    CHECK_FALSE(flowq::quic::assemble_long_packet(request).ok());

    request.number = flowq::quic::packet_number{flowq::quic::packet_number_space::handshake, 1};
    CHECK_FALSE(flowq::quic::assemble_long_packet(request).ok());

    xor_packet_protector application_protector{flowq::quic::protection_level::application};
    request.number = flowq::quic::packet_number{flowq::quic::packet_number_space::initial, 1};
    request.protector = &application_protector;
    CHECK_FALSE(flowq::quic::assemble_long_packet(request).ok());

    flowq::quic::packet_build_request handshake_request{
        flowq::quic::long_packet_type::handshake,
        1,
        cid({0xaa}),
        cid({0xbb}),
        {},
        flowq::quic::packet_number{flowq::quic::packet_number_space::handshake, 1},
        {flowq::quic::frame{flowq::quic::ping_frame{}}},
        &application_protector,
        flowq::quic::packet_pipeline_config{1200}
    };
    CHECK_FALSE(flowq::quic::assemble_long_packet(handshake_request).ok());
}

} // namespace

TEST_CASE("packet pipeline round trips Initial frames through plaintext protector") {
    flowq::quic::plaintext_packet_protector protector{};
    flowq::quic::packet_build_request request{
        flowq::quic::long_packet_type::initial,
        1,
        cid({0xaa}),
        cid({0xbb}),
        flowq::buffer{bytes({0x11})},
        flowq::quic::packet_number{flowq::quic::packet_number_space::initial, 7},
        {
            flowq::quic::frame{flowq::quic::ack_frame{4, 0, 1, {}}},
            flowq::quic::frame{flowq::quic::crypto_frame{0, flowq::buffer{bytes({0x01, 0x02})}}},
            flowq::quic::frame{flowq::quic::stream_frame{3, 0, false, true, true, flowq::buffer{bytes({0xca})}}},
            flowq::quic::frame{flowq::quic::padding_frame{2}}
        },
        &protector,
        flowq::quic::packet_pipeline_config{1200}
    };

    auto assembled = flowq::quic::assemble_long_packet(request);
    REQUIRE(assembled.ok());
    CHECK(assembled.number.value == 7);
    CHECK(assembled.protection == flowq::quic::protection_level::none);

    auto parsed = flowq::quic::parse_long_packet(assembled.datagram, protector);
    REQUIRE(parsed.ok());
    CHECK(parsed.space == flowq::quic::packet_number_space::initial);
    CHECK(parsed.number.value == 7);
    CHECK(parsed.protection == flowq::quic::protection_level::none);
    REQUIRE(std::holds_alternative<flowq::quic::initial_header>(parsed.header));
    check_buffer(std::get<flowq::quic::initial_header>(parsed.header).token, {0x11});
    REQUIRE(parsed.frames.size() == 4);
    CHECK(std::holds_alternative<flowq::quic::ack_frame>(parsed.frames[0]));
    CHECK(std::holds_alternative<flowq::quic::crypto_frame>(parsed.frames[1]));
    CHECK(std::holds_alternative<flowq::quic::stream_frame>(parsed.frames[2]));
    CHECK(std::holds_alternative<flowq::quic::padding_frame>(parsed.frames[3]));
}

TEST_CASE("packet pipeline round trips Handshake frames") {
    flowq::quic::plaintext_packet_protector protector{};
    flowq::quic::packet_build_request request{
        flowq::quic::long_packet_type::handshake,
        1,
        cid({0xaa}),
        cid({0xbb}),
        {},
        flowq::quic::packet_number{flowq::quic::packet_number_space::handshake, 9},
        {flowq::quic::frame{flowq::quic::crypto_frame{4, flowq::buffer{bytes({0xde, 0xad})}}}},
        &protector,
        flowq::quic::packet_pipeline_config{1200}
    };

    auto assembled = flowq::quic::assemble_long_packet(request);
    REQUIRE(assembled.ok());
    auto parsed = flowq::quic::parse_long_packet(assembled.datagram, protector);

    REQUIRE(parsed.ok());
    CHECK(parsed.space == flowq::quic::packet_number_space::handshake);
    CHECK(parsed.number.value == 9);
    REQUIRE(std::holds_alternative<flowq::quic::handshake_header>(parsed.header));
    REQUIRE(parsed.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::crypto_frame>(parsed.frames[0]));
}

TEST_CASE("packet pipeline round trips structural Application frames") {
    flowq::quic::plaintext_packet_protector protector{};
    flowq::quic::application_packet_build_request request{
        cid({0xaa}),
        flowq::quic::packet_number{flowq::quic::packet_number_space::application, 3},
        {
            flowq::quic::frame{flowq::quic::ping_frame{}},
            flowq::quic::frame{flowq::quic::stream_frame{0, 0, false, true, false, flowq::buffer{bytes({0x66, 0x77})}}}
        },
        &protector,
        flowq::quic::packet_pipeline_config{1200}
    };

    auto assembled = flowq::quic::assemble_application_packet(request);
    REQUIRE(assembled.ok());
    CHECK(assembled.number.space == flowq::quic::packet_number_space::application);
    CHECK(assembled.number.value == 3);
    CHECK(assembled.protection == flowq::quic::protection_level::none);

    auto parsed = flowq::quic::parse_application_packet(assembled.datagram, protector);
    REQUIRE(parsed.ok());
    CHECK(parsed.space == flowq::quic::packet_number_space::application);
    CHECK(parsed.number.value == 3);
    CHECK(parsed.protection == flowq::quic::protection_level::none);
    REQUIRE(std::holds_alternative<flowq::quic::structural_application_header>(parsed.header));
    REQUIRE(parsed.frames.size() == 2);
    CHECK(std::holds_alternative<flowq::quic::ping_frame>(parsed.frames[0]));
    REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(parsed.frames[1]));
    const auto& stream = std::get<flowq::quic::stream_frame>(parsed.frames[1]);
    CHECK(stream.stream_id == 0);
    check_buffer(stream.data, {0x66, 0x77});
}

TEST_CASE("packet pipeline rejects invalid structural Application packet metadata invariants") {
    flowq::quic::plaintext_packet_protector plaintext{};
    flowq::quic::application_packet_build_request request{
        cid({0xaa}),
        flowq::quic::packet_number{flowq::quic::packet_number_space::application, 0x1'0000'0000ULL},
        {flowq::quic::frame{flowq::quic::ping_frame{}}},
        &plaintext,
        flowq::quic::packet_pipeline_config{1200}
    };

    CHECK_FALSE(flowq::quic::assemble_application_packet(request).ok());

    request.number = flowq::quic::packet_number{flowq::quic::packet_number_space::initial, 1};
    CHECK_FALSE(flowq::quic::assemble_application_packet(request).ok());

    request.number = flowq::quic::packet_number{flowq::quic::packet_number_space::application, 1};
    request.protector = nullptr;
    CHECK_FALSE(flowq::quic::assemble_application_packet(request).ok());

    xor_packet_protector handshake_protector{flowq::quic::protection_level::handshake};
    request.protector = &handshake_protector;
    CHECK_FALSE(flowq::quic::assemble_application_packet(request).ok());
}

TEST_CASE("packet pipeline rejects malformed structural Application packets") {
    flowq::quic::plaintext_packet_protector protector{};

    CHECK_FALSE(flowq::quic::parse_application_packet(flowq::buffer{bytes({})}, protector).ok());
    CHECK_FALSE(flowq::quic::parse_application_packet(flowq::buffer{bytes({0x50, 0x01})}, protector).ok());
}

TEST_CASE("packet pipeline rejects missing protectors") {
    flowq::quic::packet_build_request request{};
    request.frames.push_back(flowq::quic::frame{flowq::quic::ping_frame{}});
    CHECK_FALSE(flowq::quic::assemble_long_packet(request).ok());

    CHECK_FALSE(flowq::quic::parse_long_packet(flowq::buffer{bytes({0xc0})}, nullptr).ok());
}

TEST_CASE("packet pipeline enforces maximum datagram size") {
    flowq::quic::plaintext_packet_protector protector{};
    flowq::quic::packet_build_request request{
        flowq::quic::long_packet_type::initial,
        1,
        cid({0xaa}),
        cid({0xbb}),
        {},
        flowq::quic::packet_number{flowq::quic::packet_number_space::initial, 1},
        {flowq::quic::frame{flowq::quic::crypto_frame{0, flowq::buffer{bytes({0x01, 0x02, 0x03, 0x04})}}}},
        &protector,
        flowq::quic::packet_pipeline_config{8}
    };

    CHECK_FALSE(flowq::quic::assemble_long_packet(request).ok());
}

TEST_CASE("packet pipeline selects complete frames by encoded payload budget") {
    const std::vector<flowq::quic::frame> candidates{
        flowq::quic::frame{flowq::quic::ping_frame{}},
        flowq::quic::frame{flowq::quic::stream_frame{0, 0, false, true, false, flowq::buffer{bytes({0x01, 0x02, 0x03})}}},
        flowq::quic::frame{flowq::quic::ping_frame{}}
    };

    auto result = flowq::quic::select_frames_for_payload_budget(candidates, 7);

    REQUIRE(result.ok());
    CHECK(result.encoded_size == 7);
    CHECK(result.next_index == 2);
    REQUIRE(result.frames.size() == 2);
    CHECK(std::holds_alternative<flowq::quic::ping_frame>(result.frames[0]));
    CHECK(std::holds_alternative<flowq::quic::stream_frame>(result.frames[1]));
}

TEST_CASE("packet pipeline returns empty budget selection before first non-fitting frame") {
    const std::vector<flowq::quic::frame> candidates{
        flowq::quic::frame{flowq::quic::stream_frame{0, 0, false, true, false, flowq::buffer{bytes({0x01, 0x02, 0x03})}}},
        flowq::quic::frame{flowq::quic::ping_frame{}}
    };

    auto result = flowq::quic::select_frames_for_payload_budget(candidates, 5);

    REQUIRE(result.ok());
    CHECK(result.encoded_size == 0);
    CHECK(result.next_index == 0);
    CHECK(result.frames.empty());
}

TEST_CASE("packet pipeline includes frame that exactly fits payload budget") {
    const std::vector<flowq::quic::frame> candidates{
        flowq::quic::frame{flowq::quic::stream_frame{0, 0, false, true, false, flowq::buffer{bytes({0x01, 0x02, 0x03})}}},
        flowq::quic::frame{flowq::quic::ping_frame{}}
    };

    auto result = flowq::quic::select_frames_for_payload_budget(candidates, 6);

    REQUIRE(result.ok());
    CHECK(result.encoded_size == 6);
    CHECK(result.next_index == 1);
    REQUIRE(result.frames.size() == 1);
    CHECK(std::holds_alternative<flowq::quic::stream_frame>(result.frames[0]));
}

TEST_CASE("packet pipeline rejects packets without fixed packet number bytes") {
    flowq::quic::initial_header header{
        std::byte{0xc0},
        1,
        cid({0xaa}),
        cid({0xbb}),
        {},
        3,
        flowq::buffer{bytes({0x00, 0x00, 0x01})}
    };
    auto encoded = flowq::quic::encode_packet_header(flowq::quic::packet_header{header});
    REQUIRE(encoded.ok());

    flowq::quic::plaintext_packet_protector protector{};
    CHECK_FALSE(flowq::quic::parse_long_packet(encoded.payload, protector).ok());
}

TEST_CASE("packet pipeline calls transforming protector") {
    xor_packet_protector protector{};
    flowq::quic::packet_build_request request{
        flowq::quic::long_packet_type::initial,
        1,
        cid({0xaa}),
        cid({0xbb}),
        {},
        flowq::quic::packet_number{flowq::quic::packet_number_space::initial, 2},
        {flowq::quic::frame{flowq::quic::ping_frame{}}},
        &protector,
        flowq::quic::packet_pipeline_config{1200}
    };

    auto assembled = flowq::quic::assemble_long_packet(request);
    REQUIRE(assembled.ok());
    CHECK(protector.protect_calls == 1);
    CHECK(assembled.protection == flowq::quic::protection_level::initial);

    auto parsed = flowq::quic::parse_long_packet(assembled.datagram, protector);
    REQUIRE(parsed.ok());
    CHECK(protector.unprotect_calls == 1);
    CHECK(parsed.protection == flowq::quic::protection_level::initial);
    REQUIRE(parsed.frames.size() == 1);
    CHECK(std::holds_alternative<flowq::quic::ping_frame>(parsed.frames[0]));
}
