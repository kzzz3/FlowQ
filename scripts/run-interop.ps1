# run-interop.ps1 - Run interop tests against peer QUIC implementations
# Usage: .\scripts\run-interop.ps1 -Peer <binary> [-Scenario <name>] [-Output <file>]
#
# This script runs interop scenarios against a specified peer QUIC implementation.
# It records results in JSON format with peer version, FlowQ commit, TLS backend version.
# Exit code 0 if all scenarios pass, 1 if any scenario fails.

param(
    [Parameter(Mandatory=$true)]
    [string]$Peer,

    [string]$Scenario,
    [string]$Output = "interop-results.json",
    [string]$BuildDir = "build/windows-msvc-vcpkg"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $RepoRoot

Write-Host "=== FlowQ Interop Runner ===" -ForegroundColor Cyan
Write-Host ""

# Gather metadata
$FlowQCommit = git rev-parse --short HEAD 2>$null
if (-not $FlowQCommit) { $FlowQCommit = "unknown" }

$FlowQBranch = git rev-parse --abbrev-ref HEAD 2>$null
if (-not $FlowQBranch) { $FlowQBranch = "unknown" }

$Timestamp = Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ"
$HostOS = $env:OS
$HostArch = $env:PROCESSOR_ARCHITECTURE

# Get peer version
Write-Host "Detecting peer version..."
$PeerVersion = "unknown"
if (Test-Path $Peer) {
    # Try common version flags
    foreach ($flag in @("--version", "-v", "-V", "version")) {
        try {
            $PeerVersion = & $Peer $flag 2>&1 | Select-Object -First 1
            if ($PeerVersion) { break }
        } catch {
            continue
        }
    }
}

if ($PeerVersion -eq "unknown") {
    Write-Host "WARNING: Could not detect peer version" -ForegroundColor Yellow
} else {
    Write-Host "Peer version: $PeerVersion" -ForegroundColor Green
}

# Get FlowQ TLS backend version
Write-Host "Detecting FlowQ TLS backend..."
$TLSBackend = "none"
if (Test-Path "include/flowq/quic/tls_provider_backend.hpp") {
    $content = Get-Content "include/flowq/quic/tls_provider_backend.hpp" -Raw
    if ($content -match "FLOWQ_ENABLE_OPENSSL_QUIC_TLS") {
        $TLSBackend = "OpenSSL (provider-backed)"
    }
}
Write-Host "TLS backend: $TLSBackend" -ForegroundColor Green

Write-Host ""
Write-Host "FlowQ commit: $FlowQCommit" -ForegroundColor Green
Write-Host "FlowQ branch: $FlowQBranch" -ForegroundColor Green
Write-Host "Timestamp: $Timestamp" -ForegroundColor Green
Write-Host "Host: $HostOS/$HostArch" -ForegroundColor Green
Write-Host ""

# Find scenarios
$ScenariosDir = "tests/interop/scenarios"
if ($Scenario) {
    $ScenarioFile = Join-Path $ScenariosDir "$Scenario.json"
    if (-not (Test-Path $ScenarioFile)) {
        Write-Host "ERROR: Scenario not found: $ScenarioFile" -ForegroundColor Red
        exit 1
    }
    $Scenarios = @($ScenarioFile)
} else {
    $Scenarios = Get-ChildItem -Path $ScenariosDir -Filter "*.json" | ForEach-Object { $_.FullName }
}

Write-Host "Running $($Scenarios.Count) scenario(s)..."
Write-Host ""

# Initialize results
$ResultsFile = $Output
$Results = @{
    metadata = @{
        timestamp = $Timestamp
        flowq_commit = $FlowQCommit
        flowq_branch = $FlowQBranch
        tls_backend = $TLSBackend
        host_os = $HostOS
        host_arch = $HostArch
    }
    peer = @{
        binary = $Peer
        version = $PeerVersion
    }
    scenarios = @()
}

$TotalScenarios = 0
$PassedScenarios = 0
$FailedScenarios = 0

foreach ($scenarioFile in $Scenarios) {
    $scenarioName = [System.IO.Path]::GetFileNameWithoutExtension($scenarioFile)
    $TotalScenarios++

    Write-Host "Running: $scenarioName" -ForegroundColor Cyan

    # Check if scenario exists
    if (-not (Test-Path $scenarioFile)) {
        Write-Host "  FAILED: Scenario file not found" -ForegroundColor Red
        $FailedScenarios++
        continue
    }

    # Check if peer binary exists
    if (-not (Test-Path $Peer)) {
        Write-Host "  FAILED: Peer binary not found" -ForegroundColor Red
        $FailedScenarios++
        continue
    }

    # Run scenario (placeholder - actual implementation would use the interop test binary)
    Write-Host "  SKIPPED: Interop test execution not yet implemented" -ForegroundColor Yellow
    Write-Host "  Scenario: $scenarioName"
    Write-Host "  Peer: $Peer ($PeerVersion)"

    # Add to results
    $Results.scenarios += @{
        name = $scenarioName
        status = "skipped"
        reason = "Interop test execution not yet implemented"
    }
}

# Write results to JSON
$Results | ConvertTo-Json -Depth 10 | Set-Content $ResultsFile

Write-Host ""
Write-Host "=== Summary ===" -ForegroundColor Cyan
Write-Host "Total scenarios: $TotalScenarios"
Write-Host "Passed: $PassedScenarios"
Write-Host "Failed: $FailedScenarios"
Write-Host "Skipped: $($TotalScenarios - $PassedScenarios - $FailedScenarios)"
Write-Host ""
Write-Host "Results written to: $ResultsFile"

# Exit with failure if any scenarios failed
if ($FailedScenarios -gt 0) {
    exit 1
}
