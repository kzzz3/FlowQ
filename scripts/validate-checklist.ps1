# validate-checklist.ps1 - Validate checklist items and forbidden wording
# Usage: .\scripts\validate-checklist.ps1 [-Fix]
#
# This script checks for:
# - Forbidden wording (production-ready, RFC-compliant, secure, interoperable)
# - TODO/FIXME comments in production code paths
# - as any or type safety suppressions
# - Empty catch blocks
# Exit code 0 if all checks pass, 1 if any violations found.

param(
    [switch]$Fix
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $RepoRoot

$Violations = 0

Write-Host "=== FlowQ Checklist Validator ===" -ForegroundColor Cyan
Write-Host "Scanning: $RepoRoot"
Write-Host ""

# 1. Check for forbidden wording in public-facing files
Write-Host "Checking for forbidden wording..." -ForegroundColor Yellow

$ForbiddenWords = @(
    "production-ready",
    "RFC-compliant",
    "interoperable",
    "secure"
)

$DocFiles = @(
    "README.md",
    "docs/README.md",
    "docs/production/release-checklist.md",
    "docs/production/readiness-gate.md",
    "docs/reference/architecture.md",
    "RELEASE_NOTES.md"
)

foreach ($word in $ForbiddenWords) {
    foreach ($file in $DocFiles) {
        if (Test-Path $file) {
            $matches = Select-String -Path $file -Pattern $word -AllMatches
            if ($matches) {
                Write-Host "WARNING: Found '$word' in $file" -ForegroundColor Yellow
                $matches | Select-Object -First 5 | ForEach-Object { Write-Host "  $_" }
                Write-Host ""
            }
        }
    }
}

# 2. Check for TODO/FIXME in production code paths
Write-Host ""
Write-Host "Checking for TODO/FIXME in production code..." -ForegroundColor Yellow

$todoCount = 0
Get-ChildItem -Path include -Filter "*.hpp" -Recurse | ForEach-Object {
    $file = $_.FullName
    $matches = Select-String -Path $file -Pattern "TODO|FIXME" -AllMatches
    if ($matches) {
        $todoCount += $matches.Count
    }
}

if ($todoCount -gt 0) {
    Write-Host "WARNING: Found $todoCount TODO/FIXME comments in production headers" -ForegroundColor Yellow
    Write-Host ""
}

# 3. Check for type safety suppressions
Write-Host ""
Write-Host "Checking for type safety suppressions..." -ForegroundColor Yellow

$asAnyCount = 0
Get-ChildItem -Path include -Filter "*.hpp" -Recurse | ForEach-Object {
    $file = $_.FullName
    $matches = Select-String -Path $file -Pattern "as any|@ts-ignore|@ts-expect-error" -AllMatches
    if ($matches) {
        $asAnyCount += $matches.Count
    }
}

if ($asAnyCount -gt 0) {
    Write-Host "VIOLATION: Found $asAnyCount type safety suppressions" -ForegroundColor Red
    $Violations += $asAnyCount
    Write-Host ""
}

# 4. Check for empty catch blocks
Write-Host ""
Write-Host "Checking for empty catch blocks..." -ForegroundColor Yellow

$emptyCatch = 0
Get-ChildItem -Path include -Filter "*.hpp" -Recurse | ForEach-Object {
    $file = $_.FullName
    $content = Get-Content $file -Raw
    $matches = [regex]::Matches($content, "catch\s*\([^)]*\)\s*\{\s*\}")
    $emptyCatch += $matches.Count
}

if ($emptyCatch -gt 0) {
    Write-Host "VIOLATION: Found $emptyCatch empty catch blocks" -ForegroundColor Red
    $Violations += $emptyCatch
    Write-Host ""
}

# 5. Check for hardcoded credentials
Write-Host ""
Write-Host "Checking for hardcoded credentials..." -ForegroundColor Yellow

$credCount = 0
$filesToCheck = Get-ChildItem -Path . -Include "*.cpp", "*.hpp", "*.h", "*.json", "*.yml", "*.yaml" -Recurse | Where-Object {
    $_.FullName -notmatch "\\node_modules\\" -and $_.FullName -notmatch "\\build\\" -and $_.FullName -notmatch "\\.git\\"
}

foreach ($file in $filesToCheck) {
    $matches = Select-String -Path $file.FullName -Pattern "(password|secret|token|key)\s*[:=]\s*['""][^'""]+['""]" -AllMatches
    if ($matches) {
        $credCount += $matches.Count
    }
}

if ($credCount -gt 0) {
    Write-Host "VIOLATION: Found $credCount potential hardcoded credentials" -ForegroundColor Red
    $Violations += $credCount
    Write-Host ""
}

Write-Host ""
Write-Host "=== Summary ===" -ForegroundColor Cyan
Write-Host "Type safety suppressions: $asAnyCount"
Write-Host "Empty catch blocks: $emptyCatch"
Write-Host "Potential hardcoded credentials: $credCount"
Write-Host "TODO/FIXME comments: $todoCount"

if ($Violations -gt 0) {
    Write-Host "FAILED: Found $Violations violations" -ForegroundColor Red
    exit 1
} else {
    Write-Host "PASSED: All checklist checks passed" -ForegroundColor Green
    exit 0
}
