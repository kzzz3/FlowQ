# FlowQ Multi-Peer Interop Test Runner
# Tests FlowQ against all available QUIC implementations

param(
    [string]$BuildDir = "build/windows-msvc-vcpkg",
    [string]$OutputDir = "docs/interop/results",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

Write-Host "FlowQ Multi-Peer Interop Test Runner" -ForegroundColor Cyan
Write-Host "=====================================" -ForegroundColor Cyan
Write-Host ""

# Create output directory
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

# Get current date and commit
$date = Get-Date -Format "yyyy-MM-dd"
$commit = & git rev-parse --short HEAD 2>$null
if (-not $commit) { $commit = "unknown" }

Write-Host "Date: $date" -ForegroundColor Gray
Write-Host "Commit: $commit" -ForegroundColor Gray
Write-Host ""

# Check available peers
$availablePeers = @()

# Check aioquic
$aioquicAvailable = $false
try {
    $aioquicVersion = & python -c "import aioquic; print(aioquic.__version__)" 2>$null
    if ($LASTEXITCODE -eq 0) {
        $aioquicAvailable = $true
        $availablePeers += @{
            Name = "aioquic"
            Version = $aioquicVersion
            Type = "python"
        }
        Write-Host "[OK] aioquic $aioquicVersion" -ForegroundColor Green
    }
} catch {
    Write-Host "[--] aioquic not available" -ForegroundColor Gray
}

# Check MsQuic (via vcpkg)
$msquicPath = "$BuildDir/vcpkg_installed/x64-windows/tools/msquic"
if (Test-Path "$msquicPath/quicping.exe") {
    $availablePeers += @{
        Name = "msquic"
        Version = "vcpkg"
        Type = "cpp"
        Binary = "$msquicPath/quicping.exe"
    }
    Write-Host "[OK] MsQuic (vcpkg)" -ForegroundColor Green
} else {
    Write-Host "[--] MsQuic not available" -ForegroundColor Gray
}

# Check ngtcp2 (via vcpkg)
$ngtcp2Path = "$BuildDir/vcpkg_installed/x64-windows/tools/ngtcp2"
if (Test-Path "$ngtcp2Path/ngtcp2-client.exe") {
    $availablePeers += @{
        Name = "ngtcp2"
        Version = "vcpkg"
        Type = "cpp"
        Binary = "$ngtcp2Path/ngtcp2-client.exe"
    }
    Write-Host "[OK] ngtcp2 (vcpkg)" -ForegroundColor Green
} else {
    Write-Host "[--] ngtcp2 not available" -ForegroundColor Gray
}

# Check lsquic (via vcpkg)
$lsquicPath = "$BuildDir/vcpkg_installed/x64-windows/tools/lsquic"
if (Test-Path "$lsquicPath/echo_client.exe") {
    $availablePeers += @{
        Name = "lsquic"
        Version = "vcpkg"
        Type = "cpp"
        Binary = "$lsquicPath/echo_client.exe"
    }
    Write-Host "[OK] lsquic (vcpkg)" -ForegroundColor Green
} else {
    Write-Host "[--] lsquic not available" -ForegroundColor Gray
}

Write-Host ""
Write-Host "Found $($availablePeers.Count) peer(s)" -ForegroundColor Cyan

if ($availablePeers.Count -eq 0) {
    Write-Host "No peers available. Skipping interop tests." -ForegroundColor Yellow
    exit 0
}

# Generate certificates if needed
$certDir = "build/certs"
$certFile = "$certDir/cert.pem"
$keyFile = "$certDir/key.pem"

if (-not (Test-Path $certFile)) {
    Write-Host "Generating test certificates..." -ForegroundColor Yellow
    & python tests/interop/test_interop.py --generate-certs
}

# Define scenarios
$scenarios = @(
    @{ Name = "basic_handshake"; Description = "TLS handshake completion" },
    @{ Name = "stream_echo"; Description = "Bidirectional stream echo" },
    @{ Name = "loss_recovery"; Description = "Packet loss recovery" }
)

# Run tests
$allResults = @()

foreach ($peer in $availablePeers) {
    Write-Host ""
    Write-Host "Testing against $($peer.Name) ($($peer.Version))" -ForegroundColor Cyan
    Write-Host "-" * 40

    foreach ($scenario in $scenarios) {
        Write-Host "  $($scenario.Name): " -NoNewline

        $result = @{
            Peer = $peer.Name
            PeerVersion = $peer.Version
            Scenario = $scenario.Name
            Date = $date
            Commit = $commit
        }

        try {
            if ($peer.Type -eq "python") {
                # Run aioquic test
                $env:FLOWQ_INTEROP_SCENARIO = $scenario.Name
                $output = & python tests/interop/test_interop.py 2>&1
                $passed = $LASTEXITCODE -eq 0
            } else {
                # Run C++ peer test
                $output = & $peer.Binary --scenario $scenario.Name --cert $certFile --key $keyFile 2>&1
                $passed = $LASTEXITCODE -eq 0
            }

            if ($passed) {
                Write-Host "PASS" -ForegroundColor Green
                $result.Result = "pass"
                $result.Details = $output -join "`n"
            } else {
                Write-Host "FAIL" -ForegroundColor Red
                $result.Result = "fail"
                $result.Error = $output -join "`n"
            }
        } catch {
            Write-Host "ERROR" -ForegroundColor Red
            $result.Result = "error"
            $result.Error = $_.Exception.Message
        }

        $allResults += $result
    }
}

# Save results
$resultsFile = "$OutputDir/interop-$date.json"
$report = @{
    date = $date
    commit = $commit
    platform = "windows-msvc-vcpkg"
    peers = $availablePeers | ForEach-Object { @{ name = $_.Name; version = $_.Version } }
    scenarios = $allResults
    summary = @{
        total = $allResults.Count
        passed = ($allResults | Where-Object { $_.Result -eq "pass" }).Count
        failed = ($allResults | Where-Object { $_.Result -eq "fail" }).Count
        errors = ($allResults | Where-Object { $_.Result -eq "error" }).Count
    }
}

$report | ConvertTo-Json -Depth 10 | Set-Content $resultsFile

# Print summary
Write-Host ""
Write-Host "Summary" -ForegroundColor Cyan
Write-Host "-------" -ForegroundColor Cyan
Write-Host "Total: $($report.summary.total)" -ForegroundColor White
Write-Host "Passed: $($report.summary.passed)" -ForegroundColor Green
Write-Host "Failed: $($report.summary.failed)" -ForegroundColor $(if ($report.summary.failed -gt 0) { "Red" } else { "Green" })
Write-Host "Errors: $($report.summary.errors)" -ForegroundColor $(if ($report.summary.errors -gt 0) { "Red" } else { "Green" })
Write-Host ""
Write-Host "Results saved to: $resultsFile" -ForegroundColor Green

# Update docs/interop/results.md
$resultsMd = @"
# FlowQ Interop Results

## $date (commit $commit)

### Summary

| Metric | Value |
|--------|-------|
| Total Tests | $($report.summary.total) |
| Passed | $($report.summary.passed) |
| Failed | $($report.summary.failed) |
| Errors | $($report.summary.errors) |

### Peer Results

| Peer | Version | Scenario | Result |
|------|---------|----------|--------|
"@

foreach ($result in $allResults) {
    $status = if ($result.Result -eq "pass") { "PASS" } elseif ($result.Result -eq "fail") { "FAIL" } else { "ERROR" }
    $resultsMd += "`n| $($result.Peer) | $($result.PeerVersion) | $($result.Scenario) | $status |"
}

$resultsMd += @"

### Environment

- Platform: Windows MSVC/vcpkg
- FlowQ TLS: OpenSSL QUIC TLS
- Cipher Suite: TLS_AES_128_GCM_SHA256
"@

$resultsMd | Set-Content "docs/interop/results.md"

# Exit with appropriate code
if ($report.summary.failed -gt 0 -or $report.summary.errors -gt 0) {
    exit 1
} else {
    exit 0
}
