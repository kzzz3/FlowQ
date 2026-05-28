# validate-build.ps1 - Run full build pipeline validation
# Usage: .\scripts\validate-build.ps1 [-SkipTests] [-Release]
#
# This script runs the complete build pipeline:
# - CMake configure
# - Build
# - Test (unless -SkipTests)
# - Install
# - Package-consumer build
# Exit code 0 if all steps succeed, 1 if any step fails.

param(
    [switch]$SkipTests,
    [switch]$Release
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $RepoRoot

$BuildType = if ($Release) { "Release" } else { "Debug" }
$Preset = "windows-msvc-vcpkg"

Write-Host "=== FlowQ Build Validator ===" -ForegroundColor Cyan
Write-Host "Repo root: $RepoRoot"
Write-Host "Build type: $BuildType"
Write-Host "Preset: $Preset"
Write-Host ""

# Step 1: Configure
Write-Host "Step 1/5: Configuring..." -ForegroundColor Yellow
if ($env:VCPKG_ROOT) {
    Write-Host "Using VCPKG_ROOT: $env:VCPKG_ROOT"
} else {
    Write-Host "WARNING: VCPKG_ROOT not set, using default" -ForegroundColor Yellow
}

cmake --preset $Preset -DCMAKE_BUILD_TYPE=$BuildType
if ($LASTEXITCODE -ne 0) {
    Write-Host "FAILED: CMake configure failed" -ForegroundColor Red
    exit 1
}
Write-Host "OK: Configure succeeded" -ForegroundColor Green
Write-Host ""

# Step 2: Build
Write-Host "Step 2/5: Building..." -ForegroundColor Yellow
cmake --build --preset $Preset --config $BuildType
if ($LASTEXITCODE -ne 0) {
    Write-Host "FAILED: Build failed" -ForegroundColor Red
    exit 1
}
Write-Host "OK: Build succeeded" -ForegroundColor Green
Write-Host ""

# Step 3: Test (unless skipped)
if (-not $SkipTests) {
    Write-Host "Step 3/5: Running tests..." -ForegroundColor Yellow
    ctest --preset $Preset --timeout 10 --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED: Tests failed" -ForegroundColor Red
        exit 1
    }
    Write-Host "OK: Tests passed" -ForegroundColor Green
} else {
    Write-Host "Step 3/5: Skipping tests (-SkipTests)" -ForegroundColor Yellow
}
Write-Host ""

# Step 4: Install
Write-Host "Step 4/5: Installing..." -ForegroundColor Yellow
$InstallDir = "build/install-flowq"
cmake --install "build/$Preset" --config $BuildType --prefix $InstallDir
if ($LASTEXITCODE -ne 0) {
    Write-Host "FAILED: Install failed" -ForegroundColor Red
    exit 1
}
Write-Host "OK: Install succeeded" -ForegroundColor Green
Write-Host ""

# Step 5: Package-consumer build
Write-Host "Step 5/5: Building package-consumer..." -ForegroundColor Yellow
$ConsumerBuildDir = "build/package-consumer"

cmake -S tests/package-consumer -B $ConsumerBuildDir -G "Visual Studio 18 2026" `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
    -DCMAKE_PREFIX_PATH="$RepoRoot/$InstallDir"
if ($LASTEXITCODE -ne 0) {
    Write-Host "FAILED: Package-consumer configure failed" -ForegroundColor Red
    exit 1
}

cmake --build $ConsumerBuildDir --config $BuildType
if ($LASTEXITCODE -ne 0) {
    Write-Host "FAILED: Package-consumer build failed" -ForegroundColor Red
    exit 1
}
Write-Host "OK: Package-consumer build succeeded" -ForegroundColor Green
Write-Host ""

Write-Host "=== Summary ===" -ForegroundColor Cyan
Write-Host "ALL PASSED: Full build pipeline succeeded" -ForegroundColor Green
Write-Host ""
Write-Host "Build artifacts:"
Write-Host "  - Build directory: build/$Preset"
Write-Host "  - Install directory: $InstallDir"
Write-Host "  - Package-consumer: $ConsumerBuildDir"
