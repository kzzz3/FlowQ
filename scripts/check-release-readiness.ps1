# check-release-readiness.ps1 - Check release readiness
# Usage: .\scripts\check-release-readiness.ps1 [-SkipBuild] [-RequireCompleteReleaseChecklist] [-SourceRoot <path>]
#
# This script runs all validation checks and reports release readiness.
# Exit code 0 if all checks pass, 1 if any check fails.

param(
    [switch]$SkipBuild,
    [switch]$RequireCompleteReleaseChecklist,
    [string]$SourceRoot
)

$ErrorActionPreference = "Stop"
if ($SourceRoot) {
    $RepoRoot = $SourceRoot
} else {
    $RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
}
Set-Location $RepoRoot

$Failed = $false

Write-Host "=== FlowQ Release Readiness Check ===" -ForegroundColor Cyan
Write-Host ""

# 1. Check documentation links
Write-Host "1. Checking documentation links..." -ForegroundColor Yellow
& .\scripts\validate-docs.ps1
if ($LASTEXITCODE -ne 0) {
    $Failed = $true
}

# 2. Check checklist items
Write-Host ""
Write-Host "2. Checking checklist items..." -ForegroundColor Yellow
& .\scripts\validate-checklist.ps1
if ($LASTEXITCODE -ne 0) {
    $Failed = $true
}

$StepNumber = 3

Write-Host ""
Write-Host "$StepNumber. Checking integration packet protection..." -ForegroundColor Yellow
$integrationBypassHits = @(
    Select-String `
        -Path (Join-Path $RepoRoot "tests\integration\*.cpp") `
        -Pattern 'plaintext_packet_protector|packet_protection_policy::test_allowed|\.protection_policy\s*='
)
if ($integrationBypassHits.Count -gt 0) {
    Write-Host "FAILED: integration tests must exercise provider-backed packet protection instead of test bypasses:" -ForegroundColor Red
    foreach ($hit in $integrationBypassHits) {
        Write-Host "  $($hit.Path):$($hit.LineNumber): $($hit.Line.Trim())" -ForegroundColor Red
    }
    $Failed = $true
}
$StepNumber += 1

if ($RequireCompleteReleaseChecklist) {
    Write-Host ""
    Write-Host "$StepNumber. Checking release checklist completion..." -ForegroundColor Yellow

    $releaseChecklist = Join-Path $RepoRoot "docs\production\release-checklist.md"
    $uncheckedItems = @(Select-String -Path $releaseChecklist -Pattern '^\s*-\s+\[\s\]\s+(.+)$')
    if ($uncheckedItems.Count -gt 0) {
        Write-Host "FAILED: Release checklist has unchecked required items:" -ForegroundColor Red
        foreach ($item in $uncheckedItems) {
            Write-Host "  $($item.Path):$($item.LineNumber): $($item.Line.Trim())" -ForegroundColor Red
        }
        $Failed = $true
    }

    $StepNumber += 1
}

# Check build (unless skipped)
if (-not $SkipBuild) {
    Write-Host ""
    Write-Host "$StepNumber. Checking build..." -ForegroundColor Yellow
    & .\scripts\validate-build.ps1 -SkipTests
    if ($LASTEXITCODE -ne 0) {
        $Failed = $true
    }
} else {
    Write-Host ""
    Write-Host "$StepNumber. Skipping build check (-SkipBuild)" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "=== Summary ===" -ForegroundColor Cyan

if ($Failed) {
    Write-Host "FAILED: Release readiness check failed" -ForegroundColor Red
    exit 1
} else {
    Write-Host "PASSED: Release readiness check passed" -ForegroundColor Green
    exit 0
}
