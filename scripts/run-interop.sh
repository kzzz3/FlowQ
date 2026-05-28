#!/bin/bash
# run-interop.sh - Run interop tests against peer QUIC implementations
# Usage: ./scripts/run-interop.sh --peer <binary> [--scenario <name>] [--output <file>]
#
# This script runs interop scenarios against a specified peer QUIC implementation.
# It records results in JSON format with peer version, FlowQ commit, TLS backend version.
# Exit code 0 if all scenarios pass, 1 if any scenario fails.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Default values
PEER_BIN=""
SCENARIO=""
OUTPUT_FILE="interop-results.json"
BUILD_DIR="build/windows-msvc-vcpkg"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --peer)
            PEER_BIN="$2"
            shift 2
            ;;
        --scenario)
            SCENARIO="$2"
            shift 2
            ;;
        --output)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

if [[ -z "$PEER_BIN" ]]; then
    echo -e "${RED}ERROR${NC}: --peer <binary> is required"
    echo "Usage: $0 --peer <binary> [--scenario <name>] [--output <file>]"
    exit 1
fi

echo -e "${CYAN}=== FlowQ Interop Runner ===${NC}"
echo ""

# Gather metadata
FLOWQ_COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
FLOWQ_BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")
TIMESTAMP=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
HOST_OS=$(uname -s 2>/dev/null || echo "unknown")
HOST_ARCH=$(uname -m 2>/dev/null || echo "unknown")

# Get peer version
echo "Detecting peer version..."
PEER_VERSION=""
if [[ -x "$PEER_BIN" ]]; then
    # Try common version flags
    for flag in "--version" "-v" "-V" "version"; do
        PEER_VERSION=$("$PEER_BIN" $flag 2>&1 | head -1 || true)
        if [[ -n "$PEER_VERSION" ]]; then
            break
        fi
    done
fi

if [[ -z "$PEER_VERSION" ]]; then
    PEER_VERSION="unknown"
    echo -e "${YELLOW}WARNING${NC}: Could not detect peer version"
else
    echo -e "Peer version: ${GREEN}${PEER_VERSION}${NC}"
fi

# Get FlowQ TLS backend version
echo "Detecting FlowQ TLS backend..."
TLS_BACKEND="none"
if [[ -f "include/flowq/quic/tls_provider_backend.hpp" ]]; then
    if grep -q "FLOWQ_ENABLE_OPENSSL_QUIC_TLS" "include/flowq/quic/tls_provider_backend.hpp"; then
        TLS_BACKEND="OpenSSL (provider-backed)"
    fi
fi
echo -e "TLS backend: ${GREEN}${TLS_BACKEND}${NC}"

echo ""
echo -e "FlowQ commit: ${GREEN}${FLOWQ_COMMIT}${NC}"
echo -e "FlowQ branch: ${GREEN}${FLOWQ_BRANCH}${NC}"
echo -e "Timestamp: ${GREEN}${TIMESTAMP}${NC}"
echo -e "Host: ${GREEN}${HOST_OS}/${HOST_ARCH}${NC}"
echo ""

# Find scenarios
SCENARIOS_DIR="tests/interop/scenarios"
if [[ -n "$SCENARIO" ]]; then
    SCENARIO_FILE="${SCENARIOS_DIR}/${SCENARIO}.json"
    if [[ ! -f "$SCENARIO_FILE" ]]; then
        echo -e "${RED}ERROR${NC}: Scenario not found: $SCENARIO_FILE"
        exit 1
    fi
    SCENARIOS=("$SCENARIO_FILE")
else
    SCENARIOS=("${SCENARIOS_DIR}"/*.json)
fi

echo "Running ${#SCENARIOS[@]} scenario(s)..."
echo ""

# Initialize results
RESULTS_FILE="$OUTPUT_FILE"
cat > "$RESULTS_FILE" <<EOF
{
    "metadata": {
        "timestamp": "${TIMESTAMP}",
        "flowq_commit": "${FLOWQ_COMMIT}",
        "flowq_branch": "${FLOWQ_BRANCH}",
        "tls_backend": "${TLS_BACKEND}",
        "host_os": "${HOST_OS}",
        "host_arch": "${HOST_ARCH}"
    },
    "peer": {
        "binary": "${PEER_BIN}",
        "version": "${PEER_VERSION}"
    },
    "scenarios": []
}
EOF

TOTAL_SCENARIOS=0
PASSED_SCENARIOS=0
FAILED_SCENARIOS=0

for scenario_file in "${SCENARIOS[@]}"; do
    scenario_name=$(basename "$scenario_file" .json)
    TOTAL_SCENARIOS=$((TOTAL_SCENARIOS + 1))

    echo -e "${CYAN}Running: ${scenario_name}${NC}"

    # Check if scenario exists
    if [[ ! -f "$scenario_file" ]]; then
        echo -e "${RED}  FAILED: Scenario file not found${NC}"
        FAILED_SCENARIOS=$((FAILED_SCENARIOS + 1))
        continue
    fi

    # Check if peer binary exists
    if [[ ! -x "$PEER_BIN" ]]; then
        echo -e "${RED}  FAILED: Peer binary not found or not executable${NC}"
        FAILED_SCENARIOS=$((FAILED_SCENARIOS + 1))
        continue
    fi

    # Run scenario (placeholder - actual implementation would use the interop test binary)
    # For now, we'll record that the scenario was attempted
    echo -e "  ${YELLOW}SKIPPED: Interop test execution not yet implemented${NC}"
    echo -e "  Scenario: ${scenario_name}"
    echo -e "  Peer: ${PEER_BIN} (${PEER_VERSION})"

    # Add to results
    jq --arg name "$scenario_name" \
       --arg status "skipped" \
       --arg reason "Interop test execution not yet implemented" \
       '.scenarios += [{"name": $name, "status": $status, "reason": $reason}]' \
       "$RESULTS_FILE" > "${RESULTS_FILE}.tmp" && mv "${RESULTS_FILE}.tmp" "$RESULTS_FILE"
done

echo ""
echo -e "${CYAN}=== Summary ===${NC}"
echo "Total scenarios: $TOTAL_SCENARIOS"
echo "Passed: $PASSED_SCENARIOS"
echo "Failed: $FAILED_SCENARIOS"
echo "Skipped: $((TOTAL_SCENARIOS - PASSED_SCENARIOS - FAILED_SCENARIOS))"
echo ""
echo "Results written to: $RESULTS_FILE"

# Exit with failure if any scenarios failed
if [[ $FAILED_SCENARIOS -gt 0 ]]; then
    exit 1
fi
