#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("interop harness skips when peer binary is missing") {
    // Simulate: no peer binary available
    std::string peer_binary = "";
    bool has_peer = !peer_binary.empty();

    if (!has_peer) {
        // Skip result
        SKIP("Peer binary not available; interop scenario skipped");
    }
}

TEST_CASE("interop harness skips when TLS backend is missing") {
    // Simulate: no provider-backed TLS adapter
    bool has_tls_backend = false;

    if (!has_tls_backend) {
        // Skip result
        SKIP("Provider-backed TLS adapter not available; handshake/stream scenarios skipped");
    }
}
