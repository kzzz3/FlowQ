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

    flowq::quic::packet_security_level security_level() const noexcept override {
        return flowq::quic::packet_security_level::authenticated_encrypted;
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

class provider_backed_packet_protector final : public flowq::quic::packet_protector {
public:
    explicit provider_backed_packet_protector(flowq::quic::protection_level level = flowq::quic::protection_level::application) : level_{level} {}

    flowq::quic::protection_level level() const noexcept override {
        return level_;
    }

    flowq::quic::packet_security_level security_level() const noexcept override {
        return flowq::quic::packet_security_level::authenticated_encrypted;
    }

    flowq::quic::crypto_provider_status provider_status() const noexcept override {
        return flowq::quic::crypto_provider_status::available(
            flowq::quic::cipher_suite::aes_128_gcm_sha256,
            flowq::quic::crypto_capabilities{
                true,
                true,
                true,
                true,
                true
            });
    }

    flowq::quic::packet_protection_result protect(std::span<const std::byte> plaintext) const override {
        return {flowq::buffer{plaintext}, {}};
    }

    flowq::quic::packet_protection_result unprotect(std::span<const std::byte> protected_payload) const override {
        return {flowq::buffer{protected_payload}, {}};
    }

private:
    flowq::quic::protection_level level_{};
};

class deterministic_header_protector final : public flowq::quic::packet_protector {
public:
    explicit deterministic_header_protector(flowq::quic::protection_level level = flowq::quic::protection_level::initial) : level_{level} {}

    flowq::quic::protection_level level() const noexcept override {
        return level_;
    }

    flowq::quic::packet_security_level security_level() const noexcept override {
        return flowq::quic::packet_security_level::authenticated_encrypted;
    }

    flowq::quic::crypto_provider_status provider_status() const noexcept override {
        return flowq::quic::crypto_provider_status::available(
            flowq::quic::cipher_suite::aes_128_gcm_sha256,
            flowq::quic::crypto_capabilities{
                true,
                true,
                true,
                true,
                true
            });
    }

    std::size_t protection_overhead() const noexcept override {
        return 16;
    }

    bool header_protection_enabled() const noexcept override {
        return true;
    }

    flowq::quic::header_protection_mask_result header_protection_mask(std::span<const std::byte> sample) const override {
        last_sample.assign(sample.begin(), sample.end());
        return {{
            std::byte{0x0b},
            std::byte{0x11},
            std::byte{0x22},
            std::byte{0x33},
            std::byte{0x44}
        }, {}};
    }

    flowq::quic::packet_protection_result protect(
        const flowq::quic::packet_protection_context& context,
        std::span<const std::byte> plaintext) const override {
        last_associated_data.assign(context.associated_data.begin(), context.associated_data.end());
        std::vector<std::byte> output{plaintext.begin(), plaintext.end()};
        output.resize(output.size() + protection_overhead(), std::byte{0x5a});
        return {flowq::buffer{std::move(output)}, {}};
    }

    flowq::quic::packet_protection_result unprotect(
        const flowq::quic::packet_protection_context& context,
        std::span<const std::byte> protected_payload) const override {
        last_associated_data.assign(context.associated_data.begin(), context.associated_data.end());
        if (protected_payload.size() < protection_overhead()) {
            return {{}, flowq::error{flowq::error_code::protocol_error, "protected payload is too short"}};
        }
        return {flowq::buffer{protected_payload.first(protected_payload.size() - protection_overhead())}, {}};
    }

    flowq::quic::packet_protection_result protect(std::span<const std::byte> plaintext) const override {
        return {flowq::buffer{plaintext}, {}};
    }

    flowq::quic::packet_protection_result unprotect(std::span<const std::byte> protected_payload) const override {
        return {flowq::buffer{protected_payload}, {}};
    }

    mutable std::vector<std::byte> last_sample;
    mutable std::vector<std::byte> last_associated_data;

private:
    flowq::quic::protection_level level_{};
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

TEST_CASE("packet pipeline applies long header protection to packet number bytes") {
    deterministic_header_protector protector{};
    flowq::quic::packet_build_request request{
        flowq::quic::long_packet_type::initial,
        1,
        cid({0x05, 0x06, 0x07, 0x08}),
        cid({0x01, 0x02, 0x03, 0x04}),
        {},
        flowq::quic::packet_number{flowq::quic::packet_number_space::initial, 0},
        {flowq::quic::frame{flowq::quic::crypto_frame{0, flowq::buffer{bytes({0x06, 0x00})}}}},
        &protector,
        flowq::quic::packet_pipeline_config{1200},
        flowq::quic::packet_protection_policy::production_required
    };

    auto assembled = flowq::quic::assemble_long_packet(request);
    REQUIRE(assembled.ok());
    constexpr std::size_t encrypted_frame_length = 5;
    const auto packet_number_offset = assembled.datagram.size() - 4 - encrypted_frame_length - protector.protection_overhead();
    REQUIRE(assembled.datagram.size() >= packet_number_offset + 4);

    const auto* data = assembled.datagram.data();
    CHECK(data[0] == std::byte{0xc8});
    CHECK(data[packet_number_offset] == std::byte{0x11});
    CHECK(data[packet_number_offset + 1] == std::byte{0x22});
    CHECK(data[packet_number_offset + 2] == std::byte{0x33});
    CHECK(data[packet_number_offset + 3] == std::byte{0x44});
    REQUIRE(protector.last_sample.size() == 16);
    CHECK(protector.last_sample[0] == std::byte{0x06});
    CHECK(protector.last_associated_data.size() == packet_number_offset + 4);
    CHECK(protector.last_associated_data[packet_number_offset] == std::byte{0x00});
    CHECK(protector.last_associated_data[packet_number_offset + 1] == std::byte{0x00});
    CHECK(protector.last_associated_data[packet_number_offset + 2] == std::byte{0x00});
    CHECK(protector.last_associated_data[packet_number_offset + 3] == std::byte{0x00});
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

TEST_CASE("packet pipeline round trips Application frames through short headers") {
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

    auto parsed = flowq::quic::parse_short_packet(assembled.datagram, 1, protector);
    REQUIRE(parsed.ok());
    CHECK(parsed.space == flowq::quic::packet_number_space::application);
    CHECK(parsed.number.value == 3);
    CHECK(parsed.protection == flowq::quic::protection_level::none);
    REQUIRE(std::holds_alternative<flowq::quic::short_header>(parsed.header));
    REQUIRE(parsed.frames.size() == 2);
    CHECK(std::holds_alternative<flowq::quic::ping_frame>(parsed.frames[0]));
    REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(parsed.frames[1]));
    const auto& stream = std::get<flowq::quic::stream_frame>(parsed.frames[1]);
    CHECK(stream.stream_id == 0);
    check_buffer(stream.data, {0x66, 0x77});
}

TEST_CASE("packet pipeline applies short header protection in production mode") {
    deterministic_header_protector protector{flowq::quic::protection_level::application};
    flowq::quic::application_packet_build_request request{
        cid({0xaa}),
        flowq::quic::packet_number{flowq::quic::packet_number_space::application, 0},
        {flowq::quic::frame{flowq::quic::stream_frame{1, 0, false, true, false, flowq::buffer{bytes({0x66})}}}},
        &protector,
        flowq::quic::packet_pipeline_config{1200},
        flowq::quic::packet_protection_policy::production_required
    };

    auto assembled = flowq::quic::assemble_application_packet(request);
    REQUIRE(assembled.ok());
    CHECK(assembled.datagram.data()[0] == std::byte{0x48});
    REQUIRE(assembled.datagram.size() >= 6);
    CHECK(assembled.datagram.data()[2] == std::byte{0x11});
    CHECK(assembled.datagram.data()[3] == std::byte{0x22});
    CHECK(assembled.datagram.data()[4] == std::byte{0x33});
    CHECK(assembled.datagram.data()[5] == std::byte{0x44});

    auto parsed = flowq::quic::parse_short_packet(
        assembled.datagram,
        1,
        &protector,
        flowq::quic::packet_protection_policy::production_required);
    REQUIRE(parsed.ok());
    CHECK(parsed.space == flowq::quic::packet_number_space::application);
    CHECK(parsed.number.value == 0);
    REQUIRE(std::holds_alternative<flowq::quic::short_header>(parsed.header));
    REQUIRE(parsed.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(parsed.frames[0]));
    const auto& stream = std::get<flowq::quic::stream_frame>(parsed.frames[0]);
    CHECK(stream.stream_id == 1);
    check_buffer(stream.data, {0x66});
    REQUIRE(protector.last_sample.size() == 16);
    CHECK(protector.last_associated_data.size() == 6);
    CHECK(protector.last_associated_data[0] == std::byte{0x43});
}

TEST_CASE("packet pipeline parses short-header shell in test mode") {
    flowq::quic::plaintext_packet_protector protector{};
    const auto parsed = flowq::quic::parse_short_packet(
        flowq::buffer{bytes({0x40, 0xaa, 0x07, 0x01})},
        1,
        protector);

    REQUIRE(parsed.ok());
    CHECK(parsed.space == flowq::quic::packet_number_space::application);
    CHECK(parsed.number.value == 7);
    REQUIRE(std::holds_alternative<flowq::quic::short_header>(parsed.header));
    const auto& header = std::get<flowq::quic::short_header>(parsed.header);
    check_buffer(header.destination_connection_id.bytes, {0xaa});
    REQUIRE(parsed.frames.size() == 1);
    CHECK(std::holds_alternative<flowq::quic::ping_frame>(parsed.frames[0]));
}

TEST_CASE("packet pipeline accepts production short-header parsing when protection policy is satisfied") {
    deterministic_header_protector protector{flowq::quic::protection_level::application};
    auto assembled = flowq::quic::assemble_application_packet(flowq::quic::application_packet_build_request{
        cid({0xaa}),
        flowq::quic::packet_number{flowq::quic::packet_number_space::application, 1},
        {flowq::quic::frame{flowq::quic::ping_frame{}}},
        &protector,
        flowq::quic::packet_pipeline_config{1200},
        flowq::quic::packet_protection_policy::production_required
    });
    REQUIRE(assembled.ok());

    CHECK(flowq::quic::parse_short_packet(
        assembled.datagram,
        1,
        &protector,
        flowq::quic::packet_protection_policy::production_required).ok());
}

TEST_CASE("packet pipeline rejects invalid Application packet metadata invariants") {
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

TEST_CASE("packet pipeline rejects packets without header protection sample") {
    flowq::quic::initial_header header{
        std::byte{0xc0},
        1,
        cid({0xaa}),
        cid({0xbb}),
        {},
        1,
        flowq::buffer{bytes({0x00})}
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

TEST_CASE("packet protectors report explicit security capability") {
    flowq::quic::plaintext_packet_protector plaintext{};
    xor_packet_protector external_adapter{};

    CHECK(plaintext.security_level() == flowq::quic::packet_security_level::test_only);
    CHECK(external_adapter.security_level() == flowq::quic::packet_security_level::authenticated_encrypted);
    CHECK_FALSE(plaintext.provider_status().production_ready());
}

TEST_CASE("packet pipeline rejects test-only protectors when production protection is required") {
    flowq::quic::plaintext_packet_protector plaintext{};
    flowq::quic::packet_build_request initial_request{
        flowq::quic::long_packet_type::initial,
        1,
        cid({0xaa}),
        cid({0xbb}),
        {},
        flowq::quic::packet_number{flowq::quic::packet_number_space::initial, 1},
        {flowq::quic::frame{flowq::quic::ping_frame{}}},
        &plaintext,
        flowq::quic::packet_pipeline_config{1200},
        flowq::quic::packet_protection_policy::production_required
    };

    auto initial = flowq::quic::assemble_long_packet(initial_request);
    CHECK_FALSE(initial.ok());
    CHECK(initial.error.code() == flowq::error_code::protocol_error);

    flowq::quic::application_packet_build_request application_request{
        cid({0xaa}),
        flowq::quic::packet_number{flowq::quic::packet_number_space::application, 1},
        {flowq::quic::frame{flowq::quic::ping_frame{}}},
        &plaintext,
        flowq::quic::packet_pipeline_config{1200},
        flowq::quic::packet_protection_policy::production_required
    };

    auto application = flowq::quic::assemble_application_packet(application_request);
    CHECK_FALSE(application.ok());
    CHECK(application.error.code() == flowq::error_code::protocol_error);
}

TEST_CASE("packet pipeline rejects authenticated encrypted adapter without provider evidence when production protection is required") {
    xor_packet_protector protector{flowq::quic::protection_level::application};
    flowq::quic::application_packet_build_request request{
        cid({0xaa}),
        flowq::quic::packet_number{flowq::quic::packet_number_space::application, 2},
        {flowq::quic::frame{flowq::quic::ping_frame{}}},
        &protector,
        flowq::quic::packet_pipeline_config{1200},
        flowq::quic::packet_protection_policy::production_required
    };

    auto assembled = flowq::quic::assemble_application_packet(request);
    CHECK_FALSE(assembled.ok());
    CHECK(assembled.error.code() == flowq::error_code::protocol_error);
}

TEST_CASE("packet pipeline accepts provider-backed authenticated encrypted adapter when production protection is required") {
    provider_backed_packet_protector protector{};
    flowq::quic::application_packet_build_request request{
        cid({0xaa}),
        flowq::quic::packet_number{flowq::quic::packet_number_space::application, 2},
        {flowq::quic::frame{flowq::quic::ping_frame{}}},
        &protector,
        flowq::quic::packet_pipeline_config{1200},
        flowq::quic::packet_protection_policy::production_required
    };

    auto assembled = flowq::quic::assemble_application_packet(request);
    REQUIRE(assembled.ok());

    auto parsed = flowq::quic::parse_short_packet(
        assembled.datagram,
        1,
        &protector,
        flowq::quic::packet_protection_policy::production_required);
    REQUIRE(parsed.ok());
    CHECK(parsed.protection == flowq::quic::protection_level::application);
}

TEST_CASE("packet pipeline rejects wrong-level provider when parsing long packets under production protection") {
    provider_backed_packet_protector initial_protector{flowq::quic::protection_level::initial};
    flowq::quic::packet_build_request initial_request{
        flowq::quic::long_packet_type::initial,
        1,
        cid({0xaa}),
        cid({0xbb}),
        {},
        flowq::quic::packet_number{flowq::quic::packet_number_space::initial, 4},
        {flowq::quic::frame{flowq::quic::ping_frame{}}},
        &initial_protector,
        flowq::quic::packet_pipeline_config{1200},
        flowq::quic::packet_protection_policy::production_required
    };
    auto initial = flowq::quic::assemble_long_packet(initial_request);
    REQUIRE(initial.ok());

    provider_backed_packet_protector application_protector{flowq::quic::protection_level::application};
    auto parsed_initial = flowq::quic::parse_long_packet(
        initial.datagram,
        &application_protector,
        flowq::quic::packet_protection_policy::production_required);
    CHECK_FALSE(parsed_initial.ok());
    CHECK(parsed_initial.error.code() == flowq::error_code::protocol_error);

    provider_backed_packet_protector handshake_protector{flowq::quic::protection_level::handshake};
    flowq::quic::packet_build_request handshake_request{
        flowq::quic::long_packet_type::handshake,
        1,
        cid({0xaa}),
        cid({0xbb}),
        {},
        flowq::quic::packet_number{flowq::quic::packet_number_space::handshake, 5},
        {flowq::quic::frame{flowq::quic::ping_frame{}}},
        &handshake_protector,
        flowq::quic::packet_pipeline_config{1200},
        flowq::quic::packet_protection_policy::production_required
    };
    auto handshake = flowq::quic::assemble_long_packet(handshake_request);
    REQUIRE(handshake.ok());

    auto parsed_handshake = flowq::quic::parse_long_packet(
        handshake.datagram,
        &initial_protector,
        flowq::quic::packet_protection_policy::production_required);
    CHECK_FALSE(parsed_handshake.ok());
    CHECK(parsed_handshake.error.code() == flowq::error_code::protocol_error);
}
