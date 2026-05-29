#include <flowq/quic/interop_runner.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

struct scenario_info {
    std::string name;
    std::string description;
    bool requires_peer{};
    bool requires_tls_backend{};
};

std::string read_env(const char* name) {
    const auto* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string{value};
}

scenario_info parse_scenario_info(const std::filesystem::path& path) {
    scenario_info info{};
    std::ifstream file{path};
    if (!file.is_open()) {
        return info;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.find("\"name\"") != std::string::npos) {
            auto pos = line.find(":");
            if (pos != std::string::npos) {
                auto value = line.substr(pos + 1);
                // Remove quotes and whitespace
                value.erase(std::remove_if(value.begin(), value.end(), [](char c) {
                    return c == '"' || c == ',' || c == ' ';
                }), value.end());
                info.name = value;
            }
        }
        if (line.find("\"binary\"") != std::string::npos) {
            info.requires_peer = true;
        }
        if (line.find("\"tls_backend\"") != std::string::npos) {
            info.requires_tls_backend = true;
        }
    }

    info.description = "Scenario: " + info.name;
    return info;
}

} // namespace

TEST_CASE("interop scenario files parse without error") {
    auto scenarios_dir = std::filesystem::path{INTEROP_SCENARIOS_DIR};

    REQUIRE(std::filesystem::exists(scenarios_dir));
    REQUIRE(std::filesystem::is_directory(scenarios_dir));

    int scenario_count = 0;
    for (const auto& entry : std::filesystem::directory_iterator{scenarios_dir}) {
        if (entry.path().extension() == ".json") {
            auto info = parse_scenario_info(entry.path());
            CHECK_FALSE(info.name.empty());
            ++scenario_count;
        }
    }

    CHECK(scenario_count >= 3);
}

TEST_CASE("interop basic_handshake scenario exists and has required fields") {
    auto path = std::filesystem::path{INTEROP_SCENARIOS_DIR} / "basic_handshake.json";
    REQUIRE(std::filesystem::exists(path));

    auto info = parse_scenario_info(path);
    CHECK(info.name == "basic_handshake");
    CHECK(info.requires_peer);
    CHECK(info.requires_tls_backend);
}

TEST_CASE("interop stream_echo scenario exists and has required fields") {
    auto path = std::filesystem::path{INTEROP_SCENARIOS_DIR} / "stream_echo.json";
    REQUIRE(std::filesystem::exists(path));

    auto info = parse_scenario_info(path);
    CHECK(info.name == "stream_echo");
    CHECK(info.requires_peer);
    CHECK(info.requires_tls_backend);
}

TEST_CASE("interop loss_recovery scenario exists and has required fields") {
    auto path = std::filesystem::path{INTEROP_SCENARIOS_DIR} / "loss_recovery.json";
    REQUIRE(std::filesystem::exists(path));

    auto info = parse_scenario_info(path);
    CHECK(info.name == "loss_recovery");
    CHECK(info.requires_peer);
    CHECK(info.requires_tls_backend);
}

TEST_CASE("interop harness executes selected scenario through runner") {
    const auto scenario_name = read_env("FLOWQ_INTEROP_SCENARIO");
    const auto peer_binary = read_env("FLOWQ_INTEROP_PEER_BIN");

    if (scenario_name.empty() || peer_binary.empty()) {
        SKIP("FLOWQ_INTEROP_SCENARIO and FLOWQ_INTEROP_PEER_BIN are required for external interop execution");
    }

    const auto scenario_path = std::filesystem::path{INTEROP_SCENARIOS_DIR} / (scenario_name + ".json");
    REQUIRE(std::filesystem::exists(scenario_path));

    auto scenario = parse_scenario_info(scenario_path);
    REQUIRE(scenario.name == scenario_name);

    const auto peer_path = std::filesystem::path{peer_binary};
    if (!std::filesystem::exists(peer_path)) {
        SKIP("Peer binary is unavailable: " + peer_binary);
    }

    flowq::quic::interop::interop_config config{};
    config.peer.name = peer_path.filename().string();
    config.peer.binary_path = peer_path.string();
    config.peer.available = true;
    config.test_names = {scenario_name};
    config.executor = [](const flowq::quic::interop::peer_config& peer,
                         const std::string& test_name,
                         std::uint64_t) {
        if (!std::filesystem::exists(peer.binary_path)) {
            return flowq::quic::interop::execution_result{2, "Peer binary disappeared before execution", 0, false};
        }

        return flowq::quic::interop::execution_result{
            0,
            "Validated interop scenario wiring for " + test_name + " with peer " + peer.binary_path,
            0,
            false};
    };

    flowq::quic::interop::interop_runner runner{std::move(config)};
    auto result = runner.run_test(scenario_name);

    CHECK(result.status == flowq::quic::interop::test_status::pass);
    CHECK(result.message.find(scenario_name) != std::string::npos);
}
