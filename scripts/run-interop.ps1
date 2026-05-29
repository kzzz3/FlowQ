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

function Resolve-PeerBinary {
    param([string]$Value)

    if (Test-Path $Value) {
        return (Resolve-Path $Value).Path
    }

    $command = Get-Command $Value -ErrorAction SilentlyContinue
    if ($command -and $command.Source) {
        return $command.Source
    }

    return $null
}

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
$HarnessPath = Join-Path $BuildDir "Debug/flowq_interop_tests.exe"
$ResolvedPeer = Resolve-PeerBinary $Peer

# Get peer version
Write-Host "Detecting peer version..."
$PeerVersion = "unknown"
if ($ResolvedPeer) {
    # Try common version flags
    foreach ($flag in @("--version", "-v", "-V", "version")) {
        try {
            $PeerVersion = & $ResolvedPeer $flag 2>&1 | Select-Object -First 1
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
Write-Host "Harness: $HarnessPath" -ForegroundColor Green
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
        resolved_binary = $ResolvedPeer
        version = $PeerVersion
    }
    scenarios = @()
}

$TotalScenarios = 0
$PassedScenarios = 0
$FailedScenarios = 0
$SkippedScenarios = 0

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

    if (-not (Test-Path $HarnessPath)) {
        Write-Host "  FAILED: Interop harness not found: $HarnessPath" -ForegroundColor Red
        $FailedScenarios++
        $Results.scenarios += @{
            name = $scenarioName
            status = "error"
            reason = "Interop harness not found"
            harness = $HarnessPath
        }
        continue
    }

    # Check if peer binary exists
    if (-not $ResolvedPeer) {
        Write-Host "  FAILED: Peer binary not found" -ForegroundColor Red
        $FailedScenarios++
        $Results.scenarios += @{
            name = $scenarioName
            status = "error"
            reason = "Peer binary not found"
            peer = $Peer
        }
        continue
    }

    $env:FLOWQ_INTEROP_PEER_BIN = $ResolvedPeer
    $env:FLOWQ_INTEROP_SCENARIO = $scenarioName

    $Stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $HarnessOutput = & $HarnessPath "*$scenarioName*" 2>&1
    $HarnessExitCode = $LASTEXITCODE
    $Stopwatch.Stop()

    $HarnessText = ($HarnessOutput | Out-String).Trim()
    $WasSkipped = $HarnessText -match "SKIPPED:"

    if ($HarnessExitCode -eq 0 -and -not $WasSkipped) {
        Write-Host "  PASSED" -ForegroundColor Green
        $PassedScenarios++
        $Status = "passed"
    } elseif ($HarnessExitCode -eq 0 -and $WasSkipped) {
        Write-Host "  SKIPPED: Harness reported scenario skip" -ForegroundColor Yellow
        $SkippedScenarios++
        $Status = "skipped"
    } else {
        Write-Host "  FAILED: Harness exit code $HarnessExitCode" -ForegroundColor Red
        $FailedScenarios++
        $Status = "failed"
    }

    $Results.scenarios += @{
        name = $scenarioName
        status = $Status
        duration_ms = $Stopwatch.ElapsedMilliseconds
        harness_exit_code = $HarnessExitCode
        output = $HarnessText
    }
}

# Write results to JSON
$Results | ConvertTo-Json -Depth 10 | Set-Content $ResultsFile

Write-Host ""
Write-Host "=== Summary ===" -ForegroundColor Cyan
Write-Host "Total scenarios: $TotalScenarios"
Write-Host "Passed: $PassedScenarios"
Write-Host "Failed: $FailedScenarios"
Write-Host "Skipped: $SkippedScenarios"
Write-Host ""
Write-Host "Results written to: $ResultsFile"

# Exit with failure if any scenarios failed
if ($FailedScenarios -gt 0) {
    exit 1
}
