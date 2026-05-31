# validate-build.ps1 - Run full build pipeline validation
# Usage: .\scripts\validate-build.ps1 [-Preset windows-msvc-vcpkg] [-BuildType Debug] [-SkipTests]
#
# This script runs the complete build pipeline:
# - CMake configure
# - Build
# - Test (unless -SkipTests)
# - Install
# - Package-consumer configure, build, and run
# Exit code 0 if all steps succeed, 1 if any step fails.

param(
    [string]$Preset = "windows-msvc-vcpkg",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$BuildType = "Debug",
    [switch]$SkipTests,
    [switch]$Release
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $RepoRoot

if ($Release) {
    $BuildType = "Release"
}

Write-Host "=== FlowQ Build Validator ===" -ForegroundColor Cyan
Write-Host "Repo root: $RepoRoot"
Write-Host "Build type: $BuildType"
Write-Host "Preset: $Preset"
Write-Host ""

$VcpkgRoot = $env:VCPKG_ROOT
if (-not $VcpkgRoot -and $Preset -like "*vcpkg*") {
    $DefaultVcpkgRoot = "D:/vcpkg"
    if (Test-Path (Join-Path $DefaultVcpkgRoot "scripts/buildsystems/vcpkg.cmake")) {
        $VcpkgRoot = $DefaultVcpkgRoot
        $env:VCPKG_ROOT = $VcpkgRoot
    }
}

if ($Preset -like "*vcpkg*") {
    if (-not $VcpkgRoot) {
        Write-Host "FAILED: VCPKG_ROOT must point to a bootstrapped vcpkg checkout for preset: $Preset" -ForegroundColor Red
        exit 1
    }

    $VcpkgToolchain = Join-Path $VcpkgRoot "scripts/buildsystems/vcpkg.cmake"
    if (-not (Test-Path $VcpkgToolchain)) {
        Write-Host "FAILED: vcpkg toolchain file not found: $VcpkgToolchain" -ForegroundColor Red
        exit 1
    }
}

# Step 1: Configure
Write-Host "Step 1/5: Configuring..." -ForegroundColor Yellow
if ($VcpkgRoot) {
    Write-Host "Using VCPKG_ROOT: $VcpkgRoot"
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
$DisallowedInstallHeaders = @(
    "include/flowq/quic/http3.hpp",
    "include/flowq/quic/http3_request.hpp",
    "include/flowq/quic/qpack.hpp",
    "include/flowq/quic/zero_rtt.hpp",
    "include/flowq/quic/interop_runner.hpp"
)
$ResolvedInstallParent = Resolve-Path -Path "build" -ErrorAction SilentlyContinue
if ($ResolvedInstallParent -and (Test-Path $InstallDir)) {
    $ResolvedInstallDir = Resolve-Path -Path $InstallDir
    if (-not $ResolvedInstallDir.Path.StartsWith($ResolvedInstallParent.Path, [System.StringComparison]::OrdinalIgnoreCase)) {
        Write-Host "FAILED: Refusing to clean install directory outside build/: $ResolvedInstallDir" -ForegroundColor Red
        exit 1
    }
    Remove-Item -LiteralPath $ResolvedInstallDir.Path -Recurse -Force
}
cmake --install "build/$Preset" --config $BuildType --prefix $InstallDir
if ($LASTEXITCODE -ne 0) {
    Write-Host "FAILED: Install failed" -ForegroundColor Red
    exit 1
}
foreach ($header in $DisallowedInstallHeaders) {
    $candidate = Join-Path $InstallDir $header
    if (Test-Path $candidate) {
        Write-Host "FAILED: Source-only/test-support header installed in production package: $header" -ForegroundColor Red
        exit 1
    }
}
Write-Host "OK: Install succeeded" -ForegroundColor Green
Write-Host ""

# Step 5: Package-consumer build and run
Write-Host "Step 5/5: Building and running package-consumer..." -ForegroundColor Yellow
$ConsumerBuildDir = "build/package-consumer"
$VcpkgTriplet = if ($env:VCPKG_TARGET_TRIPLET) { $env:VCPKG_TARGET_TRIPLET } else { "x64-windows" }
$DependencyPrefixPaths = @("$RepoRoot/$InstallDir")
if ($VcpkgToolchain) {
    $PresetVcpkgInstalled = Join-Path $RepoRoot "build/$Preset/vcpkg_installed/$VcpkgTriplet"
    if (Test-Path $PresetVcpkgInstalled) {
        $DependencyPrefixPaths += $PresetVcpkgInstalled
    }
}
$ResolvedBuildRoot = Resolve-Path -Path "build" -ErrorAction SilentlyContinue
if ($ResolvedBuildRoot -and (Test-Path $ConsumerBuildDir)) {
    $ResolvedConsumerBuildDir = Resolve-Path -Path $ConsumerBuildDir
    if (-not $ResolvedConsumerBuildDir.Path.StartsWith($ResolvedBuildRoot.Path, [System.StringComparison]::OrdinalIgnoreCase)) {
        Write-Host "FAILED: Refusing to clean package-consumer directory outside build/: $ResolvedConsumerBuildDir" -ForegroundColor Red
        exit 1
    }
    Remove-Item -LiteralPath $ResolvedConsumerBuildDir.Path -Recurse -Force
}
$ConsumerConfigureArgs = @(
    "-S", "tests/package-consumer",
    "-B", $ConsumerBuildDir,
    "-G", "Visual Studio 18 2026",
    "-DCMAKE_PREFIX_PATH=$($DependencyPrefixPaths -join ';')"
)
if ($VcpkgToolchain) {
    $ConsumerConfigureArgs += "-DCMAKE_TOOLCHAIN_FILE=$VcpkgToolchain"
}

cmake @ConsumerConfigureArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "FAILED: Package-consumer configure failed" -ForegroundColor Red
    exit 1
}

cmake --build $ConsumerBuildDir --config $BuildType
if ($LASTEXITCODE -ne 0) {
    Write-Host "FAILED: Package-consumer build failed" -ForegroundColor Red
    exit 1
}
$ConsumerExe = Join-Path $ConsumerBuildDir "$BuildType/flowq_package_consumer.exe"
if (-not (Test-Path $ConsumerExe)) {
    Write-Host "FAILED: Package-consumer executable not found: $ConsumerExe" -ForegroundColor Red
    exit 1
}

& $ConsumerExe
if ($LASTEXITCODE -ne 0) {
    Write-Host "FAILED: Package-consumer run failed" -ForegroundColor Red
    exit 1
}
Write-Host "OK: Package-consumer build and run succeeded" -ForegroundColor Green
Write-Host ""

Write-Host "=== Summary ===" -ForegroundColor Cyan
Write-Host "ALL PASSED: Full build pipeline succeeded" -ForegroundColor Green
Write-Host ""
Write-Host "Build artifacts:"
Write-Host "  - Build directory: build/$Preset"
Write-Host "  - Install directory: $InstallDir"
Write-Host "  - Package-consumer: $ConsumerBuildDir"
