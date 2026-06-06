# run-aioquic-interop.ps1 - Run FlowQ client against an aioquic server.
# Exit code 0 only when every selected aioquic scenario passes.

param(
    [string]$BuildDir = "build/windows-msvc-vcpkg-interop-openssl",
    [string]$OutputDir = "docs/interop/results",
    [string]$CondaEnv = "expr",
    [ValidateSet("bidirectional_stream", "loss_recovery", "all")]
    [string]$Scenario = "all",
    [string]$ClientPath
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $RepoRoot

function Stop-Interop {
    param([string]$Message)

    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

function Resolve-RepoPath {
    param([string]$Value)

    if ([System.IO.Path]::IsPathRooted($Value)) {
        return $Value
    }

    return Join-Path $RepoRoot $Value
}

function Invoke-NativeCapture {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Command,
        [Parameter(Mandatory=$true)]
        [string[]]$Arguments
    )

    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $output = & $Command @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    } catch {
        $output = @($_.Exception.Message)
        if ($null -ne $_.Exception.ErrorRecord -and $null -ne $_.Exception.ErrorRecord.TargetObject) {
            $output += $_.Exception.ErrorRecord.TargetObject.ToString()
        }
        $exitCode = if ($LASTEXITCODE -ne 0) { $LASTEXITCODE } else { 1 }
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = (($output | Out-String).Trim())
    }
}

$clientBinary = if ($ClientPath) {
    Resolve-RepoPath $ClientPath
} else {
    $clientName = if ($env:OS -eq "Windows_NT") { "flowq_quic_client.exe" } else { "flowq_quic_client" }
    Join-Path (Resolve-RepoPath $BuildDir) (Join-Path "Debug" $clientName)
}

if (-not (Test-Path -LiteralPath $clientBinary)) {
    Stop-Interop "FlowQ client binary not found: $clientBinary"
}

$resolvedClientBinary = (Resolve-Path -LiteralPath $clientBinary).Path
$clientDisplayPath = $resolvedClientBinary
if ($resolvedClientBinary.StartsWith($RepoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    $clientDisplayPath = $resolvedClientBinary.Substring($RepoRoot.Length).TrimStart([char[]]@('\', '/'))
}

$condaCommand = Get-Command conda -ErrorAction SilentlyContinue
if (-not $condaCommand) {
    Stop-Interop "conda command not found"
}

$condaEnvsResult = Invoke-NativeCapture -Command "conda" -Arguments @("env", "list")
if ($condaEnvsResult.ExitCode -ne 0) {
    Stop-Interop "failed to list conda environments: $($condaEnvsResult.Output)"
}

$envPattern = "^\s*$([regex]::Escape($CondaEnv))\s+"
$envFound = ($condaEnvsResult.Output -split "`r?`n") | Where-Object { $_ -match $envPattern }
if (-not $envFound) {
    Stop-Interop "conda environment not found: $CondaEnv"
}

$aioquicVersionResult = Invoke-NativeCapture -Command "conda" -Arguments @("run", "-n", $CondaEnv, "python", "-c", "import aioquic; print(aioquic.__version__)")
if ($aioquicVersionResult.ExitCode -ne 0) {
    Stop-Interop "aioquic is not importable from conda environment '$CondaEnv': $($aioquicVersionResult.Output)"
}
$aioquicVersion = (($aioquicVersionResult.Output -split "`r?`n") | Select-Object -First 1).ToString().Trim()

$scenarios = if ($Scenario -eq "all") {
    @("bidirectional_stream", "loss_recovery")
} else {
    @($Scenario)
}

if (-not (Test-Path -LiteralPath $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

$safeRepoRoot = $RepoRoot -replace "\\", "/"
$flowqCommit = & git -c "safe.directory=$safeRepoRoot" rev-parse --short HEAD 2>$null
if (-not $flowqCommit) { $flowqCommit = "unknown" }

$timestamp = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
$fileTimestamp = (Get-Date).ToUniversalTime().ToString("yyyyMMddTHHmmssZ")
$resultsFile = Join-Path $OutputDir "aioquic-$fileTimestamp-$flowqCommit.json"

Write-Host "=== FlowQ aioquic Interop Runner ===" -ForegroundColor Cyan
Write-Host "Conda env: $CondaEnv" -ForegroundColor Gray
Write-Host "aioquic: $aioquicVersion" -ForegroundColor Gray
Write-Host "FlowQ client: $clientDisplayPath" -ForegroundColor Gray
Write-Host "Scenarios: $($scenarios -join ', ')" -ForegroundColor Gray
Write-Host ""

$previousClient = $env:FLOWQ_CLIENT
$previousScenario = $env:FLOWQ_INTEROP_SCENARIO
$previousPeerHost = $env:FLOWQ_QUIC_PEER_HOST
$previousPeerPort = $env:FLOWQ_QUIC_PEER_PORT
$previousStreamPayload = $env:FLOWQ_QUIC_STREAM_PAYLOAD
$previousExpectedEcho = $env:FLOWQ_QUIC_EXPECT_ECHO
$clientPeerHost = "127.0.0.1"
$clientPeerPort = 4433
$clientStreamPayload = "hello from FlowQ"
$clientExpectedEcho = "echo from aioquic"
$results = @()

try {
    foreach ($scenarioName in $scenarios) {
        Write-Host "Running aioquic scenario: $scenarioName" -ForegroundColor Cyan
        $env:FLOWQ_CLIENT = $resolvedClientBinary
        $env:FLOWQ_INTEROP_SCENARIO = $scenarioName
        $env:FLOWQ_QUIC_PEER_HOST = $clientPeerHost
        $env:FLOWQ_QUIC_PEER_PORT = $clientPeerPort.ToString()
        $env:FLOWQ_QUIC_STREAM_PAYLOAD = $clientStreamPayload
        $env:FLOWQ_QUIC_EXPECT_ECHO = $clientExpectedEcho

        $watch = [System.Diagnostics.Stopwatch]::StartNew()
        $scenarioResult = Invoke-NativeCapture -Command "conda" -Arguments @("run", "-n", $CondaEnv, "python", "tests/interop/test_interop.py")
        $output = $scenarioResult.Output
        $exitCode = $scenarioResult.ExitCode
        $watch.Stop()

        $status = if ($exitCode -eq 0) { "passed" } else { "failed" }
        if ($exitCode -eq 0) {
            Write-Host "  PASSED" -ForegroundColor Green
        } else {
            Write-Host "  FAILED: exit code $exitCode" -ForegroundColor Red
            if ($output) {
                Write-Host $output
            }
        }

        $results += @{
            name = $scenarioName
            status = $status
            exit_code = $exitCode
            duration_ms = $watch.ElapsedMilliseconds
            output = $output
        }
    }
} finally {
    if ($null -eq $previousClient) {
        Remove-Item Env:FLOWQ_CLIENT -ErrorAction SilentlyContinue
    } else {
        $env:FLOWQ_CLIENT = $previousClient
    }

    if ($null -eq $previousScenario) {
        Remove-Item Env:FLOWQ_INTEROP_SCENARIO -ErrorAction SilentlyContinue
    } else {
        $env:FLOWQ_INTEROP_SCENARIO = $previousScenario
    }

    if ($null -eq $previousPeerHost) {
        Remove-Item Env:FLOWQ_QUIC_PEER_HOST -ErrorAction SilentlyContinue
    } else {
        $env:FLOWQ_QUIC_PEER_HOST = $previousPeerHost
    }

    if ($null -eq $previousPeerPort) {
        Remove-Item Env:FLOWQ_QUIC_PEER_PORT -ErrorAction SilentlyContinue
    } else {
        $env:FLOWQ_QUIC_PEER_PORT = $previousPeerPort
    }

    if ($null -eq $previousStreamPayload) {
        Remove-Item Env:FLOWQ_QUIC_STREAM_PAYLOAD -ErrorAction SilentlyContinue
    } else {
        $env:FLOWQ_QUIC_STREAM_PAYLOAD = $previousStreamPayload
    }

    if ($null -eq $previousExpectedEcho) {
        Remove-Item Env:FLOWQ_QUIC_EXPECT_ECHO -ErrorAction SilentlyContinue
    } else {
        $env:FLOWQ_QUIC_EXPECT_ECHO = $previousExpectedEcho
    }
}

$summary = @{
    total = $results.Count
    passed = @($results | Where-Object { $_.status -eq "passed" }).Count
    failed = @($results | Where-Object { $_.status -ne "passed" }).Count
}

$report = @{
    metadata = @{
        timestamp = $timestamp
        flowq_commit = $flowqCommit
        flowq_client = $clientDisplayPath
        conda_env = $CondaEnv
        aioquic_version = $aioquicVersion
        client_config = @{
            peer_host = $clientPeerHost
            peer_port = $clientPeerPort
            stream_payload = $clientStreamPayload
            expected_echo = $clientExpectedEcho
        }
    }
    peer = @{
        name = "aioquic"
        version = $aioquicVersion
    }
    scenarios = $results
    summary = $summary
}

$report | ConvertTo-Json -Depth 10 | Set-Content -Encoding UTF8 $resultsFile

Write-Host ""
Write-Host "=== Summary ===" -ForegroundColor Cyan
Write-Host "Total: $($summary.total)"
Write-Host "Passed: $($summary.passed)"
Write-Host "Failed: $($summary.failed)"
Write-Host "Results: $resultsFile"

if ($summary.failed -gt 0) {
    exit 1
}

exit 0
