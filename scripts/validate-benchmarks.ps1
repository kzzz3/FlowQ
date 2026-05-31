# FlowQ Benchmark Validation Script
# Validates benchmark gates for production readiness

param(
    [string]$Category = "all",
    [string]$ResultsDir = "docs/benchmarks/results",
    [switch]$GenerateReport
)

$ErrorActionPreference = "Stop"

Write-Host "FlowQ Benchmark Validation" -ForegroundColor Cyan
Write-Host "=========================" -ForegroundColor Cyan
Write-Host ""

# Define benchmark categories and their required scenarios
$benchmarkCategories = @{
    "performance" = @{
        Description = "Throughput, latency, and connection setup"
        Scenarios = @(
            "single_stream_small",
            "single_stream_large",
            "multi_stream",
            "multi_connection",
            "rtt_baseline",
            "rtt_low",
            "rtt_medium",
            "rtt_high",
            "connection_cold",
            "connection_warm"
        )
        RequiredPassRate = 1.0
    }
    "soak" = @{
        Description = "Long-running stability tests"
        Scenarios = @(
            "continuous_stream_short",
            "connection_churn",
            "idle_connection"
        )
        RequiredPassRate = 1.0
    }
    "loss" = @{
        Description = "Loss recovery and reordering"
        Scenarios = @(
            "uniform_loss_0pct",
            "uniform_loss_1pct",
            "uniform_loss_5pct",
            "uniform_loss_10pct",
            "burst_loss_small",
            "burst_loss_medium",
            "burst_loss_large",
            "reorder_minor",
            "reorder_moderate",
            "reorder_severe",
            "combined_realistic_1",
            "combined_realistic_2"
        )
        RequiredPassRate = 0.9
    }
    "migration" = @{
        Description = "Connection migration and path validation"
        Scenarios = @(
            "active_migration_single",
            "active_migration_multiple",
            "active_migration_rapid",
            "path_validation_standard",
            "path_validation_loss",
            "path_validation_timeout",
            "address_change_ip",
            "address_change_port",
            "address_change_both",
            "amplification_pre",
            "amplification_post",
            "amplification_reset",
            "server_migration_detect",
            "server_migration_spoof",
            "server_migration_nat"
        )
        RequiredPassRate = 0.9
    }
}

function Test-BenchmarkResults {
    param(
        [string]$Category,
        [hashtable]$CategoryDef
    )

    Write-Host "Checking $Category benchmarks..." -ForegroundColor Yellow

    $resultsFile = Join-Path $ResultsDir "$Category-results.json"
    if (-not (Test-Path $resultsFile)) {
        Write-Host "  [WARN] No results file found: $resultsFile" -ForegroundColor Yellow
        return @{
            Category = $Category
            Status = "NO_DATA"
            Passed = 0
            Failed = 0
            Missing = $CategoryDef.Scenarios.Count
        }
    }

    $results = Get-Content $resultsFile | ConvertFrom-Json
    $passed = 0
    $failed = 0
    $missing = 0

    foreach ($scenario in $CategoryDef.Scenarios) {
        $scenarioResult = $results.scenarios | Where-Object { $_.name -eq $scenario }
        if ($scenarioResult) {
            if ($scenarioResult.passed) {
                $passed++
            } else {
                $failed++
                Write-Host "  [FAIL] $scenario" -ForegroundColor Red
            }
        } else {
            $missing++
            Write-Host "  [MISS] $scenario" -ForegroundColor Yellow
        }
    }

    $total = $CategoryDef.Scenarios.Count
    $passRate = if ($total -gt 0) { $passed / $total } else { 0 }
    $status = if ($passRate -ge $CategoryDef.RequiredPassRate) { "PASS" } else { "FAIL" }

    Write-Host "  Results: $passed/$total passed ($([math]::Round($passRate * 100))%)" -ForegroundColor $(if ($status -eq "PASS") { "Green" } else { "Red" })

    return @{
        Category = $Category
        Status = $status
        Passed = $passed
        Failed = $failed
        Missing = $missing
        PassRate = $passRate
    }
}

# Main validation
$allResults = @()
$categoriesToCheck = if ($Category -eq "all") { $benchmarkCategories.Keys } else { @($Category) }

foreach ($cat in $categoriesToCheck) {
    if (-not $benchmarkCategories.ContainsKey($cat)) {
        Write-Host "Unknown category: $cat" -ForegroundColor Red
        continue
    }

    $result = Test-BenchmarkResults -Category $cat -CategoryDef $benchmarkCategories[$cat]
    $allResults += $result
    Write-Host ""
}

# Summary
Write-Host "Summary" -ForegroundColor Cyan
Write-Host "-------" -ForegroundColor Cyan

$totalPassed = ($allResults | Where-Object { $_.Status -eq "PASS" }).Count
$totalFailed = ($allResults | Where-Object { $_.Status -eq "FAIL" }).Count
$totalNoData = ($allResults | Where-Object { $_.Status -eq "NO_DATA" }).Count

Write-Host "Categories: $totalPassed PASS, $totalFailed FAIL, $totalNoData NO_DATA" -ForegroundColor $(if ($totalFailed -eq 0) { "Green" } else { "Red" })

# Generate report if requested
if ($GenerateReport) {
    $report = @{
        date = Get-Date -Format "yyyy-MM-dd"
        categories = $allResults
        summary = @{
            total = $allResults.Count
            passed = $totalPassed
            failed = $totalFailed
            no_data = $totalNoData
        }
    }

    $reportFile = Join-Path $ResultsDir "benchmark-report-$(Get-Date -Format 'yyyy-MM-dd').json"
    $report | ConvertTo-Json -Depth 10 | Set-Content $reportFile
    Write-Host ""
    Write-Host "Report saved to: $reportFile" -ForegroundColor Green
}

# Exit code
if ($totalFailed -gt 0) {
    Write-Host ""
    Write-Host "BENCHMARK VALIDATION FAILED" -ForegroundColor Red
    exit 1
} else {
    Write-Host ""
    Write-Host "BENCHMARK VALIDATION PASSED (or no data)" -ForegroundColor Green
    exit 0
}
