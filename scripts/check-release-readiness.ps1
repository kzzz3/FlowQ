# check-release-readiness.ps1 - Check release readiness
# Usage: .\scripts\check-release-readiness.ps1 [-SkipBuild] [-SourceRoot <path>]
#
# This script runs all validation checks and reports release readiness.
# Exit code 0 if all checks pass, 1 if any check fails.

param(
    [switch]$SkipBuild,
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

# 3. Check build (unless skipped)
if (-not $SkipBuild) {
    Write-Host ""
    Write-Host "3. Checking build..." -ForegroundColor Yellow
    & .\scripts\validate-build.ps1 -SkipTests
    if ($LASTEXITCODE -ne 0) {
        $Failed = $true
    }
} else {
    Write-Host ""
    Write-Host "3. Skipping build check (-SkipBuild)" -ForegroundColor Yellow
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
