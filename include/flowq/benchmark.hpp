#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace flowq::benchmark {

/// Benchmark result for a single run.
struct benchmark_result {
    std::string name;
    std::uint64_t iterations{};
    std::chrono::nanoseconds total_time{};
    std::chrono::nanoseconds min_time{std::chrono::nanoseconds::max()};
    std::chrono::nanoseconds max_time{};
    double avg_time_ns{};
    double ops_per_second{};
};

/// Benchmark suite runner.
class benchmark_suite {
public:
    /// Add a benchmark function.
    void add(const std::string& name, std::function<void()> fn) {
        benchmarks_.push_back({name, std::move(fn)});
    }

    /// Run all benchmarks and return results.
    [[nodiscard]] std::vector<benchmark_result> run(std::uint64_t iterations = 1000) {
        std::vector<benchmark_result> results;

        for (const auto& [name, fn] : benchmarks_) {
            benchmark_result result{};
            result.name = name;
            result.iterations = iterations;

            // Warmup
            for (std::uint64_t i = 0; i < 10; ++i) {
                fn();
            }

            // Benchmark
            auto start = std::chrono::high_resolution_clock::now();
            for (std::uint64_t i = 0; i < iterations; ++i) {
                fn();
            }
            auto end = std::chrono::high_resolution_clock::now();

            result.total_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            result.avg_time_ns = static_cast<double>(result.total_time.count()) / iterations;
            result.ops_per_second = 1e9 / result.avg_time_ns;

            // Calculate min/max by running individual iterations
            for (std::uint64_t i = 0; i < 100; ++i) {
                auto iter_start = std::chrono::high_resolution_clock::now();
                fn();
                auto iter_end = std::chrono::high_resolution_clock::now();
                auto iter_time = std::chrono::duration_cast<std::chrono::nanoseconds>(iter_end - iter_start);
                result.min_time = std::min(result.min_time, iter_time);
                result.max_time = std::max(result.max_time, iter_time);
            }

            results.push_back(result);
        }

        return results;
    }

private:
    struct benchmark_entry {
        std::string name;
        std::function<void()> fn;
    };

    std::vector<benchmark_entry> benchmarks_;
};

/// Format benchmark results as a table.
[[nodiscard]] inline std::string format_results(const std::vector<benchmark_result>& results) {
    std::string output;
    output += "Benchmark Results:\n";
    output += "==================\n\n";

    for (const auto& result : results) {
        output += "Name: " + result.name + "\n";
        output += "  Iterations: " + std::to_string(result.iterations) + "\n";
        output += "  Total Time: " + std::to_string(result.total_time.count() / 1000000) + " ms\n";
        output += "  Avg Time: " + std::to_string(result.avg_time_ns / 1000) + " us\n";
        output += "  Min Time: " + std::to_string(result.min_time.count() / 1000) + " us\n";
        output += "  Max Time: " + std::to_string(result.max_time.count() / 1000) + " us\n";
        output += "  Ops/sec: " + std::to_string(result.ops_per_second) + "\n";
        output += "\n";
    }

    return output;
}

} // namespace flowq::benchmark
