# validate-docs.ps1 - Validate documentation links and references
# Usage: .\scripts\validate-docs.ps1 [-Fix]
#
# This script scans all .md files for internal links and verifies each linked file exists.
# It reports broken links with file:line references.
# Exit code 0 if all links are valid, 1 if any broken links found.

param(
    [switch]$Fix
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $RepoRoot

$BrokenLinks = 0
$TotalLinks = 0

Write-Host "=== FlowQ Documentation Link Validator ===" -ForegroundColor Cyan
Write-Host "Scanning: $RepoRoot"
Write-Host ""

# Find all markdown files
$mdFiles = Get-ChildItem -Path . -Filter "*.md" -Recurse | Where-Object {
    $_.FullName -notmatch "\\node_modules\\" -and $_.FullName -notmatch "\\build\\"
}

foreach ($mdFile in $mdFiles) {
    $relativePath = $mdFile.FullName.Substring($RepoRoot.Length + 1).Replace('\', '/')

    # Read file and extract links
    $lineNum = 0
    $lines = Get-Content $mdFile.FullName
    foreach ($line in $lines) {
        $lineNum++

        # Extract markdown links [text](url.md)
        $matches = [regex]::Matches($line, '\[.*?\]\(([^)]+\.md)\)')
        foreach ($match in $matches) {
            $link = $match.Groups[1].Value
            $TotalLinks++

            # Resolve relative path
            if ($link.StartsWith('/')) {
                $targetFile = Join-Path $RepoRoot $link.TrimStart('/')
            } else {
                $dir = Split-Path -Parent $relativePath
                if ($dir) {
                    $targetFile = Join-Path $RepoRoot (Join-Path $dir $link)
                } else {
                    $targetFile = Join-Path $RepoRoot $link
                }
            }

            # Normalize path
            $targetFile = [System.IO.Path]::GetFullPath($targetFile)

            # Check if file exists
            if (-not (Test-Path $targetFile)) {
                Write-Host "BROKEN: ${relativePath}:${lineNum}" -ForegroundColor Red
                Write-Host "  Link: $link"
                Write-Host "  Expected: $targetFile"
                Write-Host ""
                $BrokenLinks++
            }
        }
    }
}

Write-Host ""
Write-Host "=== Summary ===" -ForegroundColor Cyan
Write-Host "Total links scanned: $TotalLinks"
Write-Host "Broken links found: $BrokenLinks"

if ($BrokenLinks -gt 0) {
    Write-Host "FAILED: Documentation has broken links" -ForegroundColor Red
    exit 1
} else {
    Write-Host "PASSED: All documentation links are valid" -ForegroundColor Green
    exit 0
}
