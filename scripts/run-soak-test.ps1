# FlowQ Soak Test Runner
# Runs long-duration stability tests

param(
    [int]$DurationHours = 1,
    [string]$OutputDir = "docs/benchmarks/results",
    [switch]$ShortMode
)

$ErrorActionPreference = "Stop"

Write-Host "FlowQ Soak Test Runner" -ForegroundColor Cyan
Write-Host "======================" -ForegroundColor Cyan
Write-Host ""

# Create output directory if it doesn't exist
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

# Get current date and commit hash
$date = Get-Date -Format "yyyy-MM-dd"
$commit = & git rev-parse --short HEAD 2>$null
if (-not $commit) { $commit = "unknown" }

# Get platform info
$platform = "windows-msvc-vcpkg"

# Calculate duration
$durationSeconds = if ($ShortMode) { 60 } else { $DurationHours * 3600 }
$sampleIntervalSeconds = 60

Write-Host "Date: $date" -ForegroundColor Gray
Write-Host "Commit: $commit" -ForegroundColor Gray
Write-Host "Platform: $platform" -ForegroundColor Gray
Write-Host "Duration: $durationSeconds seconds" -ForegroundColor Gray
Write-Host "Sample Interval: $sampleIntervalSeconds seconds" -ForegroundColor Gray
Write-Host ""

# Define soak test scenarios
$soakScenarios = @(
    @{
        Name = "continuous_stream"
        Description = "Continuous data transfer"
        TestPattern = "soak.*continuous"
    },
    @{
        Name = "connection_churn"
        Description = "Frequent connection create/destroy"
        TestPattern = "soak.*churn"
    },
    @{
        Name = "idle_connection"
        Description = "Idle connections with keep-alive"
        TestPattern = "soak.*idle"
    }
)

# Run soak tests
$allResults = @()
$startTime = Get-Date

foreach ($scenario in $soakScenarios) {
    Write-Host "Running: $($scenario.Name) - $($scenario.Description)" -ForegroundColor Yellow

    $scenarioStart = Get-Date
    $samples = @()
    $errors = 0
    $iterations = 0

    # Run for specified duration
    while ($true) {
        $elapsed = (Get-Date) - $scenarioStart
        if ($elapsed.TotalSeconds -ge $durationSeconds) {
            break
        }

        # Run a short burst of tests
        $output = & ctest --preset windows-msvc-vcpkg --timeout 10 -R "$($scenario.TestPattern)" --output-on-failure 2>&1
        $passed = $LASTEXITCODE -eq 0
        $iterations++

        if (-not $passed) {
            $errors++
            Write-Host "  [ERROR] Test failed at iteration $iterations" -ForegroundColor Red
        }

        # Collect memory sample
        $process = Get-Process -Id $PID -ErrorAction SilentlyContinue
        if ($process) {
            $samples += @{
                timestamp = Get-Date -Format "yyyy-MM-ddTHH:mm:ss"
                iteration = $iterations
                memory_mb = [math]::Round($process.WorkingSet64 / 1MB, 2)
                cpu_percent = $process.CPU
                errors = $errors
            }
        }

        # Wait before next iteration
        Start-Sleep -Seconds $sampleIntervalSeconds
    }

    $scenarioEnd = Get-Date
    $scenarioDuration = ($scenarioEnd - $scenarioStart).TotalSeconds

    $allResults += @{
        name = $scenario.Name
        description = $scenario.Description
        duration_seconds = $scenarioDuration
        iterations = $iterations
        errors = $errors
        samples = $samples
        passed = ($errors -eq 0)
    }

    Write-Host "  Completed: $iterations iterations, $errors errors" -ForegroundColor $(if ($errors -eq 0) { "Green" } else { "Red" })
}

# Create results file
$resultsFile = Join-Path $OutputDir "soak-$date.json"
$results = @{
    date = $date
    commit = $commit
    platform = $platform
    duration_hours = $DurationHours
    scenarios = $allResults
    summary = @{
        total_scenarios = $allResults.Count
        passed_scenarios = ($allResults | Where-Object { $_.passed }).Count
        failed_scenarios = ($allResults | Where-Object { -not $_.passed }).Count
        total_errors = ($allResults | Measure-Object -Property errors -Sum).Sum
    }
}

$results | ConvertTo-Json -Depth 10 | Set-Content $resultsFile

Write-Host ""
Write-Host "Summary" -ForegroundColor Cyan
Write-Host "-------" -ForegroundColor Cyan
Write-Host "Total Scenarios: $($results.summary.total_scenarios)" -ForegroundColor White
Write-Host "Passed: $($results.summary.passed_scenarios)" -ForegroundColor Green
Write-Host "Failed: $($results.summary.failed_scenarios)" -ForegroundColor $(if ($results.summary.failed_scenarios -gt 0) { "Red" } else { "Green" })
Write-Host "Total Errors: $($results.summary.total_errors)" -ForegroundColor $(if ($results.summary.total_errors -gt 0) { "Red" } else { "Green" })
Write-Host ""
Write-Host "Results saved to: $resultsFile" -ForegroundColor Green

# Exit code
if ($results.summary.failed_scenarios -gt 0 -or $results.summary.total_errors -gt 0) {
    exit 1
} else {
    exit 0
}
