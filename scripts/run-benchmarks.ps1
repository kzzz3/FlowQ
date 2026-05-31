# FlowQ Benchmark Runner
# Runs benchmark tests and records results

param(
    [string]$OutputDir = "docs/benchmarks/results",
    [string]$Category = "all",
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"

Write-Host "FlowQ Benchmark Runner" -ForegroundColor Cyan
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

Write-Host "Date: $date" -ForegroundColor Gray
Write-Host "Commit: $commit" -ForegroundColor Gray
Write-Host "Platform: $platform" -ForegroundColor Gray
Write-Host ""

# Define benchmark test patterns
$benchmarkPatterns = @{
    "performance" = @(
        "benchmark varint encode",
        "benchmark buffer operations",
        "benchmark RTT estimation",
        "benchmark congestion controller",
        "benchmark packet header encode",
        "benchmark packet encoding"
    )
    "loss" = @(
        "benchmark sent packet tracker"
    )
    "migration" = @(
        "benchmark path validation"
    )
    "key_update" = @(
        "benchmark key update state"
    )
}

# Run benchmarks
$allResults = @()
$testNames = if ($Category -eq "all") {
    $benchmarkPatterns.Values | ForEach-Object { $_ }
} else {
    $benchmarkPatterns[$Category]
}

$totalTests = $testNames.Count
$currentTest = 0

foreach ($testName in $testNames) {
    $currentTest++
    Write-Host "[$currentTest/$totalTests] Running: $testName" -ForegroundColor Yellow

    $output = & ctest --preset windows-msvc-vcpkg --timeout 30 -R "$([regex]::Escape($testName))" --output-on-failure 2>&1
    $passed = $LASTEXITCODE -eq 0

    if ($Verbose) {
        Write-Host $output -ForegroundColor Gray
    }

    $allResults += @{
        name = $testName
        passed = $passed
        timestamp = Get-Date -Format "yyyy-MM-ddTHH:mm:ss"
    }

    Write-Host "  Result: $(if ($passed) { 'PASS' } else { 'FAIL' })" -ForegroundColor $(if ($passed) { 'Green' } else { 'Red' })
}

# Create results file
$resultsFile = Join-Path $OutputDir "benchmark-$date.json"
$results = @{
    date = $date
    commit = $commit
    platform = $platform
    scenarios = $allResults
    summary = @{
        total = $allResults.Count
        passed = ($allResults | Where-Object { $_.passed }).Count
        failed = ($allResults | Where-Object { -not $_.passed }).Count
    }
}

$results | ConvertTo-Json -Depth 10 | Set-Content $resultsFile

Write-Host ""
Write-Host "Summary" -ForegroundColor Cyan
Write-Host "-------" -ForegroundColor Cyan
Write-Host "Total: $($results.summary.total)" -ForegroundColor White
Write-Host "Passed: $($results.summary.passed)" -ForegroundColor Green
Write-Host "Failed: $($results.summary.failed)" -ForegroundColor $(if ($results.summary.failed -gt 0) { "Red" } else { "Green" })
Write-Host ""
Write-Host "Results saved to: $resultsFile" -ForegroundColor Green

# Exit code
if ($results.summary.failed -gt 0) {
    exit 1
} else {
    exit 0
}
