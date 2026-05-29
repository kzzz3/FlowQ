#include <flowq/quic/connection_routing.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

flowq::quic::retry_integrity_result deterministic_retry_tag(
    const flowq::buffer& retry_pseudo_header,
    const flowq::buffer& payload) {
    std::vector<std::byte> tag;
    const auto max_size = std::max(retry_pseudo_header.size(), payload.size());
    tag.reserve(max_size);
    for (std::size_t i = 0; i < max_size; ++i) {
        auto a = i < retry_pseudo_header.size() ? retry_pseudo_header.data()[i] : std::byte{0};
        auto b = i < payload.size() ? payload.data()[i] : std::byte{0};
        tag.push_back(a ^ b);
    }
    return {flowq::buffer{tag}, {}};
}

flowq::endpoint retry_peer(std::string host = "203.0.113.10", std::uint16_t port = 4433) {
    return flowq::endpoint{std::move(host), port, "hq-interop"};
}

} // namespace

TEST_CASE("routing table looks up connection by destination connection ID") {
    flowq::quic::routing_table table{};
    flowq::quic::connection_id cid_a{flowq::buffer{std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}}}};
    flowq::quic::connection_id cid_b{flowq::buffer{std::vector<std::byte>{std::byte{0x03}, std::byte{0x04}}}};

    table.add(cid_a, 100);
    table.add(cid_b, 200);

    auto result = table.lookup(cid_a);
    REQUIRE(result.has_value());
    CHECK(*result == 100);

    auto result_b = table.lookup(cid_b);
    REQUIRE(result_b.has_value());
    CHECK(*result_b == 200);
}

TEST_CASE("routing table returns nullopt for unknown connection ID") {
    flowq::quic::routing_table table{};
    flowq::quic::connection_id known{flowq::buffer{std::vector<std::byte>{std::byte{0x01}}}};
    flowq::quic::connection_id unknown{flowq::buffer{std::vector<std::byte>{std::byte{0xff}}}};

    table.add(known, 42);

    auto result = table.lookup(unknown);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("routing table removes connection ID on retirement") {
    flowq::quic::routing_table table{};
    flowq::quic::connection_id cid{flowq::buffer{std::vector<std::byte>{std::byte{0x01}}}};

    table.add(cid, 42);
    REQUIRE(table.lookup(cid).has_value());

    table.retire(cid);
    CHECK_FALSE(table.lookup(cid).has_value());
}

TEST_CASE("routing table reports active connection count") {
    flowq::quic::routing_table table{};
    flowq::quic::connection_id cid_a{flowq::buffer{std::vector<std::byte>{std::byte{0x01}}}};
    flowq::quic::connection_id cid_b{flowq::buffer{std::vector<std::byte>{std::byte{0x02}}}};

    CHECK(table.active_count() == 0);

    table.add(cid_a, 1);
    CHECK(table.active_count() == 1);

    table.add(cid_b, 2);
    CHECK(table.active_count() == 2);

    table.retire(cid_a);
    CHECK(table.active_count() == 1);
}

TEST_CASE("routing table overwrites existing connection ID mapping") {
    flowq::quic::routing_table table{};
    flowq::quic::connection_id cid{flowq::buffer{std::vector<std::byte>{std::byte{0x01}}}};

    table.add(cid, 1);
    table.add(cid, 2);

    auto result = table.lookup(cid);
    REQUIRE(result.has_value());
    CHECK(*result == 2);
    CHECK(table.active_count() == 1);
}

TEST_CASE("version negotiation rejects empty supported versions") {
    flowq::quic::connection_id dst{flowq::buffer{std::vector<std::byte>{std::byte{0x01}}}};
    flowq::quic::connection_id src{flowq::buffer{std::vector<std::byte>{std::byte{0x02}}}};

    auto packet = flowq::quic::build_version_negotiation(dst, src, {});

    CHECK_FALSE(packet.ok());
}

TEST_CASE("version negotiation selects matching version from supported set") {
    std::vector<std::uint32_t> supported{0x00000001, 0x00000002};

    CHECK(flowq::quic::version_supported(0x00000001, supported));
    CHECK(flowq::quic::version_supported(0x00000002, supported));
    CHECK_FALSE(flowq::quic::version_supported(0x00000003, supported));
    CHECK_FALSE(flowq::quic::version_supported(0x00000000, supported));
}

TEST_CASE("version negotiation builds Version Negotiation packet from unsupported version") {
    flowq::quic::connection_id dst{flowq::buffer{std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}}}};
    flowq::quic::connection_id src{flowq::buffer{std::vector<std::byte>{std::byte{0x03}, std::byte{0x04}}}};
    std::vector<std::uint32_t> supported{0x00000001};

    auto packet = flowq::quic::build_version_negotiation(dst, src, supported);

    REQUIRE(packet.ok());
    CHECK_FALSE(packet.payload.empty());
    CHECK(packet.destination_connection_id.bytes.size() == 2);
    CHECK(packet.source_connection_id.bytes.size() == 2);
    CHECK(packet.supported_versions == supported);
}

TEST_CASE("version negotiation packet includes all supported versions") {
    flowq::quic::connection_id dst{flowq::buffer{std::vector<std::byte>{std::byte{0x01}}}};
    flowq::quic::connection_id src{flowq::buffer{std::vector<std::byte>{std::byte{0x02}}}};
    std::vector<std::uint32_t> versions{0x00000001, 0x00000002, 0x00000003};

    auto packet = flowq::quic::build_version_negotiation(dst, src, versions);

    REQUIRE(packet.ok());
    CHECK(packet.supported_versions.size() == 3);
}

TEST_CASE("version negotiation packet has correct wire format with version zero and CID lengths") {
    flowq::quic::connection_id dst{flowq::buffer{std::vector<std::byte>{std::byte{0xaa}, std::byte{0xbb}}}};
    flowq::quic::connection_id src{flowq::buffer{std::vector<std::byte>{std::byte{0xcc}}}};
    std::vector<std::uint32_t> versions{0x00000001};

    auto packet = flowq::quic::build_version_negotiation(dst, src, versions);
    REQUIRE(packet.ok());

    const auto& p = packet.payload;
    REQUIRE(p.size() >= 11);  // 1 + 4 + 1 + 2 + 1 + 1 + 4
    CHECK(p.data()[0] == std::byte{0x80});   // header form
    CHECK(p.data()[1] == std::byte{0x00});   // version = 0
    CHECK(p.data()[2] == std::byte{0x00});
    CHECK(p.data()[3] == std::byte{0x00});
    CHECK(p.data()[4] == std::byte{0x00});
    CHECK(p.data()[5] == std::byte{0x02});   // DCID length = 2
    CHECK(p.data()[6] == std::byte{0xaa});   // DCID byte 0
    CHECK(p.data()[7] == std::byte{0xbb});   // DCID byte 1
    CHECK(p.data()[8] == std::byte{0x01});   // SCID length = 1
    CHECK(p.data()[9] == std::byte{0xcc});   // SCID byte 0
    CHECK(p.data()[10] == std::byte{0x00});  // version 0x00000001
    CHECK(p.data()[11] == std::byte{0x00});
    CHECK(p.data()[12] == std::byte{0x00});
    CHECK(p.data()[13] == std::byte{0x01});
}

TEST_CASE("retry token validation interface accepts valid token shape") {
    flowq::quic::retry_token token{};
    token.data = flowq::buffer{std::vector<std::byte>{std::byte{0xaa}, std::byte{0xbb}}};

    flowq::quic::retry_token_validator validator{};

    CHECK(validator.validate_token_shape(token));
}

TEST_CASE("retry token validation interface rejects empty token") {
    flowq::quic::retry_token token{};

    flowq::quic::retry_token_validator validator{};

    CHECK_FALSE(validator.validate_token_shape(token));
}

TEST_CASE("retry integrity tag fails closed without crypto provider") {
    flowq::quic::retry_integrity_provider provider{};

    auto result = provider.compute_integrity_tag(
        flowq::buffer{std::vector<std::byte>{std::byte{0x01}}},
        flowq::buffer{std::vector<std::byte>{std::byte{0x02}}});

    CHECK_FALSE(result.ok());
    CHECK(result.tag.empty());
}

TEST_CASE("retry integrity verification fails closed without crypto provider") {
    flowq::quic::retry_integrity_provider provider{};

    auto verified = provider.verify_integrity_tag(
        flowq::buffer{std::vector<std::byte>{std::byte{0x01}}},
        flowq::buffer{std::vector<std::byte>{std::byte{0xaa}, std::byte{0xbb}}},
        flowq::buffer{std::vector<std::byte>{std::byte{0xcc}}});

    CHECK_FALSE(verified.ok());
}

TEST_CASE("retry integrity verification accepts matching tag from configured crypto provider") {
    flowq::quic::retry_integrity_provider provider{deterministic_retry_tag};

    auto computed = provider.compute_integrity_tag(
        flowq::buffer{std::vector<std::byte>{std::byte{0x01}}},
        flowq::buffer{std::vector<std::byte>{std::byte{0xaa}, std::byte{0xbb}}});
    REQUIRE(computed.ok());

    auto verified = provider.verify_integrity_tag(
        flowq::buffer{std::vector<std::byte>{std::byte{0x01}}},
        flowq::buffer{std::vector<std::byte>{std::byte{0xaa}, std::byte{0xbb}}},
        computed.tag);

    CHECK(verified.ok());
}

TEST_CASE("retry integrity verification rejects tampered payload from configured crypto provider") {
    flowq::quic::retry_integrity_provider provider{deterministic_retry_tag};

    auto computed = provider.compute_integrity_tag(
        flowq::buffer{std::vector<std::byte>{std::byte{0x01}}},
        flowq::buffer{std::vector<std::byte>{std::byte{0xaa}, std::byte{0xbb}}});
    REQUIRE(computed.ok());

    auto verified = provider.verify_integrity_tag(
        flowq::buffer{std::vector<std::byte>{std::byte{0x01}}},
        flowq::buffer{std::vector<std::byte>{std::byte{0xaa}, std::byte{0xbc}}},
        computed.tag);

    CHECK_FALSE(verified.ok());
}

TEST_CASE("retry integrity constant-time equality accepts identical tags") {
    const auto expected = flowq::buffer{std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}}};
    const auto candidate = flowq::buffer{std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}}};

    CHECK(flowq::quic::detail::constant_time_equal(
        std::span<const std::byte>{expected.data(), expected.size()},
        std::span<const std::byte>{candidate.data(), candidate.size()}));
}

TEST_CASE("retry integrity constant-time equality rejects shorter matching prefix") {
    const auto expected = flowq::buffer{std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}}};
    const auto candidate = flowq::buffer{std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}}};

    CHECK_FALSE(flowq::quic::detail::constant_time_equal(
        std::span<const std::byte>{expected.data(), expected.size()},
        std::span<const std::byte>{candidate.data(), candidate.size()}));
}

TEST_CASE("retry integrity constant-time equality rejects longer matching prefix") {
    const auto expected = flowq::buffer{std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}}};
    const auto candidate = flowq::buffer{std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}}};

    CHECK_FALSE(flowq::quic::detail::constant_time_equal(
        std::span<const std::byte>{expected.data(), expected.size()},
        std::span<const std::byte>{candidate.data(), candidate.size()}));
}

TEST_CASE("retry integrity verification rejects empty tag") {
    flowq::quic::retry_integrity_provider provider{};

    auto verified = provider.verify_integrity_tag(
        flowq::buffer{std::vector<std::byte>{std::byte{0x01}}},
        flowq::buffer{std::vector<std::byte>{std::byte{0xaa}, std::byte{0xbb}}},
        flowq::buffer{});

    CHECK_FALSE(verified.ok());
}

TEST_CASE("retry token issue fails closed without integrity provider") {
    flowq::quic::retry_token_validator validator{};
    flowq::quic::retry_token_context context{
        retry_peer(),
        flowq::quic::connection_id{flowq::buffer{std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}}}},
        std::chrono::system_clock::time_point{std::chrono::seconds{1000}}
    };

    auto issued = validator.issue_token(context);

    CHECK_FALSE(issued.ok());
    CHECK(issued.token.data.empty());
}

TEST_CASE("retry token validates matching peer address and original destination connection ID") {
    flowq::quic::retry_token_validator validator{
        flowq::quic::retry_integrity_provider{deterministic_retry_tag},
        flowq::quic::retry_token_validator_config{std::chrono::seconds{60}}
    };
    flowq::quic::retry_token_context context{
        retry_peer(),
        flowq::quic::connection_id{flowq::buffer{std::vector<std::byte>{std::byte{0xaa}, std::byte{0xbb}}}},
        std::chrono::system_clock::time_point{std::chrono::seconds{1000}}
    };

    auto issued = validator.issue_token(context);
    REQUIRE(issued.ok());
    REQUIRE_FALSE(issued.token.data.empty());

    auto validated = validator.validate_token(issued.token, context);

    CHECK(validated.ok());
}

TEST_CASE("retry token rejects a different peer address") {
    flowq::quic::retry_token_validator validator{
        flowq::quic::retry_integrity_provider{deterministic_retry_tag},
        flowq::quic::retry_token_validator_config{std::chrono::seconds{60}}
    };
    flowq::quic::retry_token_context context{
        retry_peer(),
        flowq::quic::connection_id{flowq::buffer{std::vector<std::byte>{std::byte{0xaa}, std::byte{0xbb}}}},
        std::chrono::system_clock::time_point{std::chrono::seconds{1000}}
    };
    auto issued = validator.issue_token(context);
    REQUIRE(issued.ok());

    auto moved_context = context;
    moved_context.remote = retry_peer("203.0.113.11", 4433);

    auto validated = validator.validate_token(issued.token, moved_context);

    CHECK_FALSE(validated.ok());
}

TEST_CASE("retry token rejects a different original destination connection ID") {
    flowq::quic::retry_token_validator validator{
        flowq::quic::retry_integrity_provider{deterministic_retry_tag},
        flowq::quic::retry_token_validator_config{std::chrono::seconds{60}}
    };
    flowq::quic::retry_token_context context{
        retry_peer(),
        flowq::quic::connection_id{flowq::buffer{std::vector<std::byte>{std::byte{0xaa}, std::byte{0xbb}}}},
        std::chrono::system_clock::time_point{std::chrono::seconds{1000}}
    };
    auto issued = validator.issue_token(context);
    REQUIRE(issued.ok());

    auto wrong_dcid_context = context;
    wrong_dcid_context.original_destination_connection_id = flowq::quic::connection_id{flowq::buffer{std::vector<std::byte>{std::byte{0xcc}}}};

    auto validated = validator.validate_token(issued.token, wrong_dcid_context);

    CHECK_FALSE(validated.ok());
}

TEST_CASE("retry token rejects expired token") {
    flowq::quic::retry_token_validator validator{
        flowq::quic::retry_integrity_provider{deterministic_retry_tag},
        flowq::quic::retry_token_validator_config{std::chrono::seconds{60}}
    };
    flowq::quic::retry_token_context context{
        retry_peer(),
        flowq::quic::connection_id{flowq::buffer{std::vector<std::byte>{std::byte{0xaa}, std::byte{0xbb}}}},
        std::chrono::system_clock::time_point{std::chrono::seconds{1000}}
    };
    auto issued = validator.issue_token(context);
    REQUIRE(issued.ok());

    auto late_context = context;
    late_context.now += std::chrono::seconds{61};

    auto validated = validator.validate_token(issued.token, late_context);

    CHECK_FALSE(validated.ok());
}

TEST_CASE("retry token rejects tampered payload") {
    flowq::quic::retry_token_validator validator{
        flowq::quic::retry_integrity_provider{deterministic_retry_tag},
        flowq::quic::retry_token_validator_config{std::chrono::seconds{60}}
    };
    flowq::quic::retry_token_context context{
        retry_peer(),
        flowq::quic::connection_id{flowq::buffer{std::vector<std::byte>{std::byte{0xaa}, std::byte{0xbb}}}},
        std::chrono::system_clock::time_point{std::chrono::seconds{1000}}
    };
    auto issued = validator.issue_token(context);
    REQUIRE(issued.ok());
    REQUIRE(issued.token.data.size() > 8);

    auto tampered = issued.token;
    tampered.data[8] = static_cast<std::byte>(static_cast<unsigned char>(tampered.data[8]) ^ 0x01U);

    auto validated = validator.validate_token(tampered, context);

    CHECK_FALSE(validated.ok());
}
