# validate-checklist.ps1 - Validate checklist items and forbidden wording
# Usage: .\scripts\validate-checklist.ps1 [-Fix]
#
# This script checks for:
# - Forbidden wording (production-ready, RFC-compliant, secure, interoperable)
# - TODO/FIXME comments in production code paths
# - as any or type safety suppressions
# - Empty catch blocks
# - Documentation comments on selected public QUIC APIs
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

function Get-RepoRelativePath {
    param([string]$Path)
    $repoFull = (Resolve-Path $RepoRoot).Path.TrimEnd('\', '/')
    $resolved = (Resolve-Path $Path).Path
    if ($resolved.StartsWith($repoFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $resolved.Substring($repoFull.Length).TrimStart('\', '/').Replace('\', '/')
    }
    return $resolved.Replace('\', '/')
}

function Test-PolicyDocument {
    param([string]$Path)
    $normalized = $Path.Replace('\', '/')
    return $normalized -eq "docs/production/release-checklist.md" -or
           $normalized -eq "docs/production/readiness-gate.md"
}

function Test-NegatedForbiddenClaim {
    param([string]$Line)
    $normalized = $Line.ToLowerInvariant()
    return $normalized -match "\bnot\s+production-ready\b" -or
           $normalized -match "\bnot\s+production\s+quic\b" -or
           $normalized -match "\bwithout\s+claiming\b" -or
           $normalized -match "\bexplicitly\s+deferred\b" -or
           $normalized -match "\bno\s+.*\bguarantees\b"
}

$ExperimentalPublicHeaders = @(
    "include/flowq/quic/http3.hpp",
    "include/flowq/quic/http3_request.hpp",
    "include/flowq/quic/qpack.hpp",
    "include/flowq/quic/zero_rtt.hpp"
)

$DocumentedPublicApiHeaders = @(
    "include/flowq/quic/recovery_scheduler.hpp",
    "include/flowq/quic/lifecycle_scheduler.hpp",
    "include/flowq/quic/timer_scheduler.hpp"
)

function Test-SnakeCaseIdentifier {
    param([string]$Name)
    return $Name -cmatch "^[a-z][a-z0-9_]*$" -and
           $Name -cnotmatch "__" -and
           $Name -cnotmatch "_$"
}

function Test-ExperimentalPublicHeader {
    param([string]$Path)
    $relative = Get-RepoRelativePath $Path
    return $ExperimentalPublicHeaders -contains $relative
}

function Test-DocumentedPublicApiDeclaration {
    param([string]$Line)
    return $Line -match "^(enum class|struct|class)\s+[A-Za-z_][A-Za-z0-9_]*\b" -or
           $Line -match '^\[\[nodiscard\]\]\s+(inline\s+)?(?:auto|[A-Za-z_:][A-Za-z0-9_:<>,&*\s]*?)\s+[A-Za-z_][A-Za-z0-9_]*\s*\('
}

function Test-HasLeadingDocComment {
    param(
        [string[]]$Lines,
        [int]$Index
    )

    for ($lineIndex = $Index - 1; $lineIndex -ge 0; $lineIndex -= 1) {
        $previous = $Lines[$lineIndex].Trim()
        if ($previous.Length -eq 0 -or $previous -match "^template\s*<" -or $previous -match "^\[\[.*\]\]$") {
            continue
        }
        return $previous.StartsWith("///") -or $previous.StartsWith("/**")
    }
    return $false
}

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

$forbiddenClaimCount = 0
foreach ($word in $ForbiddenWords) {
    foreach ($file in $DocFiles) {
        if (Test-Path $file) {
            $matches = Select-String -Path $file -Pattern $word -AllMatches
            if ($matches) {
                foreach ($match in $matches) {
                    if ((Test-PolicyDocument $file) -or (Test-NegatedForbiddenClaim $match.Line)) {
                        continue
                    }

                    Write-Host "VIOLATION: Forbidden production claim '$word' in $file" -ForegroundColor Red
                    Write-Host "  $match"
                    $forbiddenClaimCount += 1
                }
            }
        }
    }
}

if ($forbiddenClaimCount -gt 0) {
    $Violations += $forbiddenClaimCount
    Write-Host ""
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
    Write-Host "VIOLATION: Found $todoCount TODO/FIXME comments in production headers" -ForegroundColor Red
    $Violations += $todoCount
    Write-Host ""
}

# 3. Check for implementation placeholders in production public headers
Write-Host ""
Write-Host "Checking for placeholder implementations in production headers..." -ForegroundColor Yellow

$PlaceholderPatterns = @(
    "simulate successful",
    "not implemented",
    "For now",
    "stub implementation",
    "Deterministic stub",
    "always succeeds"
)

$placeholderCount = 0
Get-ChildItem -Path include -Filter "*.hpp" -Recurse | ForEach-Object {
    if (Test-ExperimentalPublicHeader $_.FullName) {
        return
    }

    foreach ($pattern in $PlaceholderPatterns) {
        $matches = Select-String -Path $_.FullName -Pattern $pattern -AllMatches
        if ($matches) {
            $placeholderCount += $matches.Count
            $relative = Get-RepoRelativePath $_.FullName
            Write-Host "VIOLATION: Found placeholder wording '$pattern' in $relative" -ForegroundColor Red
            $matches | Select-Object -First 5 | ForEach-Object { Write-Host "  $_" }
        }
    }
}

if ($placeholderCount -gt 0) {
    $Violations += $placeholderCount
    Write-Host ""
}

# 4. Check for type safety suppressions
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

# 5. Check for empty catch blocks
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

# 6. Check for hardcoded credentials
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

# 7. Check for public API documentation comments on selected production QUIC headers
Write-Host ""
Write-Host "Checking public QUIC API documentation comments..." -ForegroundColor Yellow

$publicApiDocGapCount = 0
foreach ($file in $DocumentedPublicApiHeaders) {
    if (!(Test-Path $file)) {
        continue
    }

    $lines = Get-Content $file
    $inDetailNamespace = $false
    for ($lineIndex = 0; $lineIndex -lt $lines.Count; $lineIndex += 1) {
        $trimmed = $lines[$lineIndex].Trim()
        if ($trimmed -eq "namespace detail {") {
            $inDetailNamespace = $true
            continue
        }
        if ($trimmed -eq "} // namespace detail") {
            $inDetailNamespace = $false
            continue
        }
        if ($inDetailNamespace -or !(Test-DocumentedPublicApiDeclaration $trimmed)) {
            continue
        }

        if (!(Test-HasLeadingDocComment $lines $lineIndex)) {
            $publicApiDocGapCount += 1
            $lineNumber = $lineIndex + 1
            Write-Host "VIOLATION: Public API declaration lacks documentation in ${file}:$lineNumber" -ForegroundColor Red
            Write-Host "  $trimmed"
        }
    }
}

if ($publicApiDocGapCount -gt 0) {
    $Violations += $publicApiDocGapCount
    Write-Host ""
}

# 8. Check public QUIC API naming conventions
Write-Host ""
Write-Host "Checking public QUIC API naming conventions..." -ForegroundColor Yellow

$namingViolationCount = 0
Get-ChildItem -Path include/flowq/quic -Filter "*.hpp" -Recurse | ForEach-Object {
    $file = $_.FullName
    if (Test-ExperimentalPublicHeader $file) {
        return
    }

    $lines = Get-Content $file
    $inDetailNamespace = $false
    for ($lineIndex = 0; $lineIndex -lt $lines.Count; $lineIndex += 1) {
        $trimmed = $lines[$lineIndex].Trim()
        if ($trimmed -eq "namespace detail {") {
            $inDetailNamespace = $true
            continue
        }
        if ($trimmed -eq "} // namespace detail") {
            $inDetailNamespace = $false
            continue
        }
        if ($inDetailNamespace) {
            continue
        }

        $typeMatch = [regex]::Match($trimmed, "^(enum class|struct|class)\s+([A-Za-z_][A-Za-z0-9_]*)\b")
        if ($typeMatch.Success) {
            $name = $typeMatch.Groups[2].Value
            if (!(Test-SnakeCaseIdentifier $name)) {
                $namingViolationCount += 1
                $lineNumber = $lineIndex + 1
                $relative = Get-RepoRelativePath $file
                Write-Host "VIOLATION: Public API type is not snake_case in ${relative}:$lineNumber" -ForegroundColor Red
                Write-Host "  $trimmed"
            }
            continue
        }

        $functionMatch = [regex]::Match($trimmed, '^\[\[nodiscard\]\]\s+(?:inline\s+)?(?:static\s+)?(?:virtual\s+)?(?:auto|[A-Za-z_:][A-Za-z0-9_:<>,&*\s]*?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(')
        if ($functionMatch.Success) {
            $name = $functionMatch.Groups[1].Value
            if (!(Test-SnakeCaseIdentifier $name)) {
                $namingViolationCount += 1
                $lineNumber = $lineIndex + 1
                $relative = Get-RepoRelativePath $file
                Write-Host "VIOLATION: Public API function is not snake_case in ${relative}:$lineNumber" -ForegroundColor Red
                Write-Host "  $trimmed"
            }
        }
    }
}

if ($namingViolationCount -gt 0) {
    $Violations += $namingViolationCount
    Write-Host ""
}

Write-Host ""
Write-Host "=== Summary ===" -ForegroundColor Cyan
Write-Host "Type safety suppressions: $asAnyCount"
Write-Host "Empty catch blocks: $emptyCatch"
Write-Host "Potential hardcoded credentials: $credCount"
Write-Host "TODO/FIXME comments: $todoCount"
Write-Host "Forbidden production claims: $forbiddenClaimCount"
Write-Host "Placeholder implementation wording: $placeholderCount"
Write-Host "Public API documentation gaps: $publicApiDocGapCount"
Write-Host "Public API naming violations: $namingViolationCount"

if ($Violations -gt 0) {
    Write-Host "FAILED: Found $Violations violations" -ForegroundColor Red
    exit 1
} else {
    Write-Host "PASSED: All checklist checks passed" -ForegroundColor Green
    exit 0
}
