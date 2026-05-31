#include <flowq/quic/endpoint_driver.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>

namespace {

flowq::quic::connection_id make_cid(std::initializer_list<unsigned char> bytes) {
    std::vector<std::byte> data;
    data.reserve(bytes.size());
    for (auto b : bytes) {
        data.push_back(static_cast<std::byte>(b));
    }
    return flowq::quic::connection_id{flowq::buffer{data}};
}

flowq::quic::stateless_reset_token make_reset_token(std::uint8_t seed) {
    flowq::quic::stateless_reset_token token{};
    for (std::size_t index = 0; index < token.size(); ++index) {
        token[index] = static_cast<std::byte>(seed + index);
    }
    return token;
}

flowq::quic::random_bytes_result deterministic_reset_random(std::size_t size) {
    std::vector<std::byte> data(size);
    for (std::size_t index = 0; index < data.size(); ++index) {
        data[index] = static_cast<std::byte>(0x80U + (index & 0x3FU));
    }
    return flowq::quic::random_bytes_result{flowq::buffer{data}, flowq::error{}};
}

bool ends_with_token(const flowq::buffer& payload,
                     const flowq::quic::stateless_reset_token& token) {
    if (payload.size() < token.size()) {
        return false;
    }

    const auto* bytes = payload.data() + (payload.size() - token.size());
    for (std::size_t index = 0; index < token.size(); ++index) {
        if (bytes[index] != token[index]) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("endpoint driver starts and stops cleanly") {
    flowq::quic::endpoint_driver_config config{};
    config.max_connections = 10;

    flowq::quic::endpoint_driver driver{config};

    CHECK_FALSE(driver.running());
    driver.start();
    CHECK(driver.running());
    driver.stop();
    CHECK_FALSE(driver.running());
}

TEST_CASE("endpoint driver stops cleanly even when not started") {
    flowq::quic::endpoint_driver driver{flowq::quic::endpoint_driver_config{}};

    driver.stop();  // no-op when not started
    CHECK_FALSE(driver.running());
}

TEST_CASE("endpoint driver stop is idempotent") {
    flowq::quic::endpoint_driver driver{flowq::quic::endpoint_driver_config{}};

    driver.start();
    driver.stop();
    driver.stop();  // second stop is a no-op

    CHECK_FALSE(driver.running());
}

TEST_CASE("endpoint driver registers and looks up connections by CID") {
    flowq::quic::endpoint_driver driver{flowq::quic::endpoint_driver_config{}};
    driver.start();

    auto cid = make_cid({0x01, 0x02});
    auto reg = driver.register_connection(cid, 42);
    CHECK(reg.ok());

    auto result = driver.lookup_connection(cid);
    REQUIRE(result.has_value());
    CHECK(*result == 42);

    driver.stop();
}

TEST_CASE("endpoint driver returns nullopt for unregistered CID") {
    flowq::quic::endpoint_driver driver{flowq::quic::endpoint_driver_config{}};
    driver.start();

    auto result = driver.lookup_connection(make_cid({0xff}));
    CHECK_FALSE(result.has_value());

    driver.stop();
}

TEST_CASE("endpoint driver removes connection on unregister") {
    flowq::quic::endpoint_driver driver{flowq::quic::endpoint_driver_config{}};
    driver.start();

    auto cid = make_cid({0x01});
    (void)driver.register_connection(cid, 42);
    driver.unregister_connection(cid);

    CHECK_FALSE(driver.lookup_connection(cid).has_value());

    driver.stop();
}

TEST_CASE("endpoint driver enforces max connections limit") {
    flowq::quic::endpoint_driver_config config{};
    config.max_connections = 2;
    flowq::quic::endpoint_driver driver{config};
    driver.start();

    (void)driver.register_connection(make_cid({0x01}), 1);
    (void)driver.register_connection(make_cid({0x02}), 2);

    auto overflow = driver.register_connection(make_cid({0x03}), 3);
    CHECK_FALSE(overflow.ok());

    driver.stop();
}

TEST_CASE("endpoint driver tracks active connection count") {
    flowq::quic::endpoint_driver driver{flowq::quic::endpoint_driver_config{}};
    driver.start();

    CHECK(driver.connection_count() == 0);

    (void)driver.register_connection(make_cid({0x01}), 1);
    CHECK(driver.connection_count() == 1);

    (void)driver.register_connection(make_cid({0x02}), 2);
    CHECK(driver.connection_count() == 2);

    driver.unregister_connection(make_cid({0x01}));
    CHECK(driver.connection_count() == 1);

    driver.stop();
}

TEST_CASE("endpoint driver rejects operations when not running") {
    flowq::quic::endpoint_driver driver{flowq::quic::endpoint_driver_config{}};

    auto result = driver.register_connection(make_cid({0x01}), 1);
    CHECK_FALSE(result.ok());
}

TEST_CASE("endpoint driver config exposes max connections") {
    flowq::quic::endpoint_driver_config config{};
    config.max_connections = 500;

    flowq::quic::endpoint_driver driver{config};

    CHECK(driver.config().max_connections == 500);
}

TEST_CASE("endpoint driver builds stateless reset for retired registered cid",
          "[quic][endpoint][stateless-reset]") {
    flowq::quic::endpoint_driver_config config{};
    config.stateless_reset.preferred_datagram_size = 41;
    config.stateless_reset.random = deterministic_reset_random;
    flowq::quic::endpoint_driver driver{config};
    driver.start();

    const auto cid = make_cid({0x31, 0x32, 0x33, 0x34});
    const auto token = make_reset_token(0xA0);
    REQUIRE(driver.register_connection(cid, 7, token).ok());
    driver.unregister_connection(cid);

    const auto reset = driver.build_stateless_reset(cid, 1200);
    REQUIRE(reset.ok());
    REQUIRE(reset.payload.size() == 41);
    REQUIRE((std::to_integer<std::uint8_t>(reset.payload.data()[0]) & 0x80U) == 0U);
    REQUIRE((std::to_integer<std::uint8_t>(reset.payload.data()[0]) & 0x40U) == 0x40U);
    REQUIRE(ends_with_token(reset.payload, token));
}

TEST_CASE("endpoint driver does not reset active or unknown connection ids",
          "[quic][endpoint][stateless-reset]") {
    flowq::quic::endpoint_driver_config config{};
    config.stateless_reset.random = deterministic_reset_random;
    flowq::quic::endpoint_driver driver{config};
    driver.start();

    const auto active_cid = make_cid({0x41, 0x42, 0x43, 0x44});
    const auto unknown_cid = make_cid({0x51, 0x52, 0x53, 0x54});
    REQUIRE(driver.register_connection(active_cid, 9, make_reset_token(0xB0)).ok());

    const auto active_reset = driver.build_stateless_reset(active_cid, 1200);
    REQUIRE_FALSE(active_reset.ok());

    const auto unknown_reset = driver.build_stateless_reset(unknown_cid, 1200);
    REQUIRE_FALSE(unknown_reset.ok());
}

TEST_CASE("endpoint driver keeps stateless resets smaller than the triggering datagram",
          "[quic][endpoint][stateless-reset]") {
    flowq::quic::endpoint_driver_config config{};
    config.stateless_reset.preferred_datagram_size = 41;
    config.stateless_reset.random = deterministic_reset_random;
    flowq::quic::endpoint_driver driver{config};
    driver.start();

    const auto cid = make_cid({0x61, 0x62, 0x63, 0x64});
    const auto token = make_reset_token(0xC0);
    REQUIRE(driver.register_connection(cid, 11, token).ok());
    driver.unregister_connection(cid);

    const auto reset = driver.build_stateless_reset(cid, 30);
    REQUIRE(reset.ok());
    REQUIRE(reset.payload.size() == 29);
    REQUIRE(ends_with_token(reset.payload, token));

    const auto too_small =
        driver.build_stateless_reset(cid, flowq::quic::minimum_stateless_reset_datagram_size);
    REQUIRE_FALSE(too_small.ok());
}

TEST_CASE("endpoint driver reports stopped state after construction") {
    flowq::quic::endpoint_driver driver{flowq::quic::endpoint_driver_config{}};

    CHECK_FALSE(driver.running());
    CHECK(driver.connection_count() == 0);
}
