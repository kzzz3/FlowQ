#include "interop_runner.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

TEST_CASE("interop_runner constructs with config") {
    flowq::quic::interop::interop_config config{};
    config.peer.name = "ngtcp2";
    config.peer.version = "0.9.0";
    config.peer.binary_path = "/usr/bin/ngtcp2";
    config.peer.available = false;

    flowq::quic::interop::interop_runner runner{config};

    CHECK_FALSE(runner.peer_available());
    CHECK(runner.peer().name == "ngtcp2");
    CHECK(runner.peer().version == "0.9.0");
}

TEST_CASE("interop_runner skips tests when peer unavailable") {
    flowq::quic::interop::interop_config config{};
    config.peer.name = "ngtcp2";
    config.peer.available = false;
    config.test_names = {"handshake", "stream_echo"};

    flowq::quic::interop::interop_runner runner{config};

    auto result = runner.run_all();

    CHECK(result.total_tests == 2);
    CHECK(result.skipped == 2);
    CHECK(result.passed == 0);
    CHECK(result.failed == 0);
}

TEST_CASE("interop_runner runs available peer through executor") {
    flowq::quic::interop::interop_config config{};
    config.peer.name = "ngtcp2";
    config.peer.version = "1.0.0";
    config.peer.binary_path = "/usr/bin/ngtcp2";
    config.peer.available = true;
    config.timeout_ms = 1234;

    bool executor_called = false;
    config.executor = [&](const flowq::quic::interop::peer_config& peer,
                          const std::string& test_name,
                          std::uint64_t timeout_ms) {
        executor_called = true;
        CHECK(peer.name == "ngtcp2");
        CHECK(peer.binary_path == "/usr/bin/ngtcp2");
        CHECK(test_name == "handshake");
        CHECK(timeout_ms == 1234);

        return flowq::quic::interop::execution_result{0, "scenario passed", 17, false};
    };

    flowq::quic::interop::interop_runner runner{config};

    auto result = runner.run_test("handshake");

    CHECK(executor_called);
    CHECK(result.test_name == "handshake");
    CHECK(result.status == flowq::quic::interop::test_status::pass);
    CHECK(result.message == "scenario passed");
    CHECK(result.duration_ms == 17);
}

TEST_CASE("interop_runner reports error when available peer has no executor") {
    flowq::quic::interop::interop_config config{};
    config.peer.name = "ngtcp2";
    config.peer.available = true;

    flowq::quic::interop::interop_runner runner{config};

    auto result = runner.run_test("handshake");

    CHECK(result.test_name == "handshake");
    CHECK(result.status == flowq::quic::interop::test_status::error);
    CHECK(result.message.find("executor") != std::string::npos);
}

TEST_CASE("interop_runner maps executor outcomes into suite result") {
    flowq::quic::interop::interop_config config{};
    config.peer.name = "ngtcp2";
    config.peer.available = true;
    config.test_names = {"handshake", "stream_echo", "loss_recovery", "version_negotiation"};
    config.executor = [](const flowq::quic::interop::peer_config&,
                         const std::string& test_name,
                         std::uint64_t) {
        if (test_name == "handshake") {
            return flowq::quic::interop::execution_result{0, "ok", 11, false};
        }

        if (test_name == "stream_echo") {
            return flowq::quic::interop::execution_result{42, "peer rejected stream data", 12, false};
        }

        if (test_name == "loss_recovery") {
            return flowq::quic::interop::execution_result{0, "deadline exceeded", 30000, true};
        }

        throw std::runtime_error{"executor crashed"};
    };

    flowq::quic::interop::interop_runner runner{config};

    auto result = runner.run_all();

    REQUIRE(result.results.size() == 4);
    CHECK(result.total_tests == 4);
    CHECK(result.passed == 1);
    CHECK(result.failed == 1);
    CHECK(result.skipped == 0);
    CHECK(result.errors == 2);
    CHECK(result.results[0].status == flowq::quic::interop::test_status::pass);
    CHECK(result.results[1].status == flowq::quic::interop::test_status::fail);
    CHECK(result.results[2].status == flowq::quic::interop::test_status::error);
    CHECK(result.results[3].status == flowq::quic::interop::test_status::error);
    CHECK(result.results[3].message.find("executor crashed") != std::string::npos);
}

TEST_CASE("interop_runner format_suite_result") {
    flowq::quic::interop::suite_result result{};
    result.suite_name = "ngtcp2 interop tests";
    result.total_tests = 3;
    result.passed = 1;
    result.failed = 1;
    result.skipped = 1;

    result.results.push_back({"handshake", flowq::quic::interop::test_status::pass, "", 0});
    result.results.push_back({"stream_echo", flowq::quic::interop::test_status::fail, "Connection timeout", 0});
    result.results.push_back({"loss_recovery", flowq::quic::interop::test_status::skip, "Peer unavailable", 0});

    auto formatted = flowq::quic::interop::format_suite_result(result);

    CHECK_FALSE(formatted.empty());
    CHECK(formatted.find("ngtcp2 interop tests") != std::string::npos);
    CHECK(formatted.find("Total: 3") != std::string::npos);
    CHECK(formatted.find("Passed: 1") != std::string::npos);
    CHECK(formatted.find("Failed: 1") != std::string::npos);
    CHECK(formatted.find("Skipped: 1") != std::string::npos);
    CHECK(formatted.find("[PASS] handshake") != std::string::npos);
    CHECK(formatted.find("[FAIL] stream_echo") != std::string::npos);
    CHECK(formatted.find("[SKIP] loss_recovery") != std::string::npos);
}

TEST_CASE("interop_config default values") {
    flowq::quic::interop::interop_config config{};

    CHECK(config.peer.name.empty());
    CHECK(config.peer.version.empty());
    CHECK(config.peer.binary_path.empty());
    CHECK_FALSE(config.peer.available);
    CHECK(config.test_names.empty());
    CHECK(config.timeout_ms == 30000);
    CHECK_FALSE(config.executor);
}

TEST_CASE("test_status enum values") {
    CHECK(static_cast<int>(flowq::quic::interop::test_status::pass) == 0);
    CHECK(static_cast<int>(flowq::quic::interop::test_status::fail) == 1);
    CHECK(static_cast<int>(flowq::quic::interop::test_status::skip) == 2);
    CHECK(static_cast<int>(flowq::quic::interop::test_status::error) == 3);
}
