#!/bin/bash
# validate-build.sh - Run full build pipeline validation
# Usage: ./scripts/validate-build.sh [--preset <name>] [--skip-tests] [--release]
#
# This script runs the complete build pipeline:
# - CMake configure
# - Build
# - Test (unless --skip-tests)
# - Install
# - Package-consumer build
# Exit code 0 if all steps succeed, 1 if any step fails.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

SKIP_TESTS=false
BUILD_TYPE="Debug"
PRESET=""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

usage() {
    echo "Usage: ./scripts/validate-build.sh [--preset <name>] [--skip-tests] [--release]"
}

if [[ "$(uname -s)" == "Linux" ]]; then
    PRESET="linux-gcc-vcpkg"
else
    PRESET="windows-msvc-vcpkg"
fi

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --preset)
            if [[ $# -lt 2 ]]; then
                usage
                exit 1
            fi
            PRESET="$2"
            shift 2
            ;;
        --skip-tests)
            SKIP_TESTS=true
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo -e "${RED}FAILED${NC}: Unknown argument: $1"
            usage
            exit 1
            ;;
    esac
done

echo "=== FlowQ Build Validator ==="
echo "Repo root: $REPO_ROOT"
echo "Build type: $BUILD_TYPE"
echo "Preset: $PRESET"
echo ""

# Step 1: Configure
echo "Step 1/5: Configuring..."
if [[ -n "$VCPKG_ROOT" ]]; then
    echo "Using VCPKG_ROOT: $VCPKG_ROOT"
else
    echo -e "${YELLOW}WARNING${NC}: VCPKG_ROOT not set, using default"
fi

cmake --preset "$PRESET" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
echo -e "${GREEN}OK${NC}: Configure succeeded"
echo ""

# Step 2: Build
echo "Step 2/5: Building..."
cmake --build --preset "$PRESET" --config "$BUILD_TYPE"
echo -e "${GREEN}OK${NC}: Build succeeded"
echo ""

# Step 3: Test (unless skipped)
if [[ "$SKIP_TESTS" == false ]]; then
    echo "Step 3/5: Running tests..."
    ctest --preset "$PRESET" --timeout 10 --output-on-failure
    echo -e "${GREEN}OK${NC}: Tests passed"
else
    echo "Step 3/5: Skipping tests (--skip-tests)"
fi
echo ""

# Step 4: Install
echo "Step 4/5: Installing..."
INSTALL_DIR="build/install-flowq"
cmake --install "build/$PRESET" --config "$BUILD_TYPE" --prefix "$INSTALL_DIR"
echo -e "${GREEN}OK${NC}: Install succeeded"
echo ""

# Step 5: Package-consumer build
echo "Step 5/5: Building package-consumer..."
CONSUMER_BUILD_DIR="build/package-consumer"

# Detect generator
if [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "cygwin" ]] || [[ "$PRESET" == windows-* ]]; then
    GENERATOR="Visual Studio 18 2026"
else
    GENERATOR="Ninja"
fi

cmake -S tests/package-consumer -B "$CONSUMER_BUILD_DIR" -G "$GENERATOR" \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DCMAKE_PREFIX_PATH="$REPO_ROOT/$INSTALL_DIR"

cmake --build "$CONSUMER_BUILD_DIR" --config "$BUILD_TYPE"
echo -e "${GREEN}OK${NC}: Package-consumer build succeeded"
echo ""

echo "=== Summary ==="
echo -e "${GREEN}ALL PASSED${NC}: Full build pipeline succeeded"
echo ""
echo "Build artifacts:"
echo "  - Build directory: build/$PRESET"
echo "  - Install directory: $INSTALL_DIR"
echo "  - Package-consumer: $CONSUMER_BUILD_DIR"
