#include <flowq/quic/endpoint_driver.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
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

} // namespace

TEST_CASE("endpoint driver starts and stops cleanly") {
    flowq::quic::endpoint_driver_config config{};
    config.max_connections = 10;
    config.max_send_queue_size = 100;

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

TEST_CASE("endpoint driver config exposes max send queue size") {
    flowq::quic::endpoint_driver_config config{};
    config.max_send_queue_size = 500;

    flowq::quic::endpoint_driver driver{config};

    CHECK(driver.config().max_send_queue_size == 500);
}

TEST_CASE("endpoint driver reports stopped state after construction") {
    flowq::quic::endpoint_driver driver{flowq::quic::endpoint_driver_config{}};

    CHECK_FALSE(driver.running());
    CHECK(driver.connection_count() == 0);
}
