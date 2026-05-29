#pragma once

#include <flowq/error.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace flowq::quic::interop {

/// Interop test result status.
enum class test_status {
    pass,
    fail,
    skip,
    error
};

/// Interop test result.
struct test_result {
    std::string test_name;
    test_status status{test_status::skip};
    std::string message;
    std::uint64_t duration_ms{};
};

/// Interop test suite result.
struct suite_result {
    std::string suite_name;
    std::vector<test_result> results;
    std::uint64_t total_tests{};
    std::uint64_t passed{};
    std::uint64_t failed{};
    std::uint64_t skipped{};
    std::uint64_t errors{};
};

/// Interop peer configuration.
struct peer_config {
    std::string name;
    std::string version;
    std::string binary_path;
    bool available{};
};

/// Interop test configuration.
struct interop_config {
    peer_config peer;
    std::vector<std::string> test_names;
    std::uint64_t timeout_ms{30000};
};

/// @warning This is a structural stub for testing only. NOT production-ready.
/// Interop test runner.
/// Provides a framework for testing FlowQ against other QUIC implementations.
class interop_runner {
public:
    explicit interop_runner(interop_config config)
        : config_{std::move(config)} {}

    /// Check if the peer is available.
    [[nodiscard]] bool peer_available() const noexcept {
        return config_.peer.available;
    }

    /// Get peer configuration.
    [[nodiscard]] const peer_config& peer() const noexcept {
        return config_.peer;
    }

    /// Run all configured tests.
    [[nodiscard]] suite_result run_all() {
        suite_result result{};
        result.suite_name = config_.peer.name + " interop tests";

        for (const auto& test_name : config_.test_names) {
            auto test_result = run_test(test_name);
            result.results.push_back(test_result);
            result.total_tests++;

            switch (test_result.status) {
            case test_status::pass:
                result.passed++;
                break;
            case test_status::fail:
                result.failed++;
                break;
            case test_status::skip:
                result.skipped++;
                break;
            case test_status::error:
                result.errors++;
                break;
            }
        }

        return result;
    }

    /// Run a specific test.
    [[nodiscard]] test_result run_test(const std::string& test_name) {
        test_result result{};
        result.test_name = test_name;

        if (!config_.peer.available) {
            result.status = test_status::skip;
            result.message = "Peer not available: " + config_.peer.name;
            return result;
        }

        // In a real implementation, this would:
        // 1. Launch peer process
        // 2. Run test scenario
        // 3. Collect results
        // 4. Return pass/fail

        // For now, return skip
        result.status = test_status::skip;
        result.message = "Interop test not implemented: " + test_name;
        return result;
    }

private:
    interop_config config_;
};

/// Format suite result as string.
[[nodiscard]] inline std::string format_suite_result(const suite_result& result) {
    std::string output;
    output += "Suite: " + result.suite_name + "\n";
    output += "Total: " + std::to_string(result.total_tests) + "\n";
    output += "Passed: " + std::to_string(result.passed) + "\n";
    output += "Failed: " + std::to_string(result.failed) + "\n";
    output += "Skipped: " + std::to_string(result.skipped) + "\n";
    output += "Errors: " + std::to_string(result.errors) + "\n\n";

    for (const auto& test : result.results) {
        output += "  [" + std::to_string(static_cast<int>(test.status)) + "] " + test.test_name;
        if (!test.message.empty()) {
            output += " - " + test.message;
        }
        output += "\n";
    }

    return output;
}

} // namespace flowq::quic::interop
