#!/bin/bash
# validate-sanitizers.sh - Run the ASan/UBSan validation gate.
# Usage: ./scripts/validate-sanitizers.sh [--preset <name>]

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

PRESET="linux-asan-ubsan"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

usage() {
    echo "Usage: ./scripts/validate-sanitizers.sh [--preset <name>]"
}

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

if [[ "$(uname -s)" != "Linux" ]]; then
    echo -e "${RED}FAILED${NC}: Sanitizer validation requires a Linux host."
    exit 1
fi

if [[ -z "$VCPKG_ROOT" ]]; then
    echo -e "${RED}FAILED${NC}: VCPKG_ROOT must point to a bootstrapped vcpkg checkout."
    exit 1
fi

echo "=== FlowQ ASan/UBSan Validator ==="
echo "Repo root: $REPO_ROOT"
echo "Preset: $PRESET"
echo "VCPKG_ROOT: $VCPKG_ROOT"
echo ""

echo "Step 1/3: Configuring sanitizer build..."
cmake --preset "$PRESET"
echo -e "${GREEN}OK${NC}: Configure succeeded"
echo ""

echo "Step 2/3: Building sanitizer build..."
cmake --build --preset "$PRESET"
echo -e "${GREEN}OK${NC}: Build succeeded"
echo ""

echo "Step 3/3: Running tests under ASan/UBSan..."
ctest --preset "$PRESET" --timeout 30 --output-on-failure
echo -e "${GREEN}OK${NC}: Sanitizer test gate passed"
