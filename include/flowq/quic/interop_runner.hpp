#pragma once

#include <flowq/error.hpp>

#include <cstdint>
#include <exception>
#include <functional>
#include <string>
#include <utility>
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

/// Result returned by an interop scenario executor.
struct execution_result {
    int exit_code{};
    std::string output;
    std::uint64_t duration_ms{};
    bool timed_out{};
};

using scenario_executor = std::function<execution_result(
    const peer_config& peer,
    const std::string& test_name,
    std::uint64_t timeout_ms)>;

/// Interop test configuration.
struct interop_config {
    peer_config peer;
    std::vector<std::string> test_names;
    std::uint64_t timeout_ms{30000};
    scenario_executor executor;
};

/// Interop test runner.
/// Coordinates FlowQ scenario execution against other QUIC implementations.
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
            auto current = run_test(test_name);
            result.results.push_back(current);
            result.total_tests++;

            switch (current.status) {
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

        if (!config_.executor) {
            result.status = test_status::error;
            result.message = "Interop executor missing for available peer: " + config_.peer.name;
            return result;
        }

        try {
            const auto execution = config_.executor(config_.peer, test_name, config_.timeout_ms);
            result.duration_ms = execution.duration_ms;
            result.message = execution.output;

            if (execution.timed_out) {
                result.status = test_status::error;
                if (result.message.empty()) {
                    result.message = "Interop scenario timed out after " +
                                     std::to_string(config_.timeout_ms) + " ms";
                }
                return result;
            }

            if (execution.exit_code == 0) {
                result.status = test_status::pass;
                return result;
            }

            result.status = test_status::fail;
            if (result.message.empty()) {
                result.message = "Interop scenario exited with code " +
                                 std::to_string(execution.exit_code);
            }
        } catch (const std::exception& exception) {
            result.status = test_status::error;
            result.message = "Interop executor error: ";
            result.message += exception.what();
        } catch (...) {
            result.status = test_status::error;
            result.message = "Interop executor error";
        }

        return result;
    }

private:
    interop_config config_;
};

[[nodiscard]] inline std::string status_label(test_status status) {
    switch (status) {
    case test_status::pass:
        return "PASS";
    case test_status::fail:
        return "FAIL";
    case test_status::skip:
        return "SKIP";
    case test_status::error:
        return "ERROR";
    }

    return "UNKNOWN";
}

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
        output += "  [" + status_label(test.status) + "] " + test.test_name;
        if (!test.message.empty()) {
            output += " - " + test.message;
        }
        output += "\n";
    }

    return output;
}

} // namespace flowq::quic::interop
