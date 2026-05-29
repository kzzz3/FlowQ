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

resolve_harness_binary() {
    local candidate
    for candidate in \
        "${BUILD_DIR}/Debug/flowq_interop_tests.exe" \
        "${BUILD_DIR}/Debug/flowq_interop_tests" \
        "${BUILD_DIR}/flowq_interop_tests"; do
        if [[ -x "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    printf '%s\n' "${BUILD_DIR}/Debug/flowq_interop_tests.exe"
}

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
HARNESS_BIN=$(resolve_harness_binary)

if [[ -x "$PEER_BIN" ]]; then
    PEER_RESOLVED="$(cd "$(dirname "$PEER_BIN")" && pwd)/$(basename "$PEER_BIN")"
else
    PEER_RESOLVED=$(command -v "$PEER_BIN" 2>/dev/null || true)
fi

# Get peer version
echo "Detecting peer version..."
PEER_VERSION=""
if [[ -n "$PEER_RESOLVED" ]]; then
    # Try common version flags
    for flag in "--version" "-v" "-V" "version"; do
        PEER_VERSION=$("$PEER_RESOLVED" $flag 2>&1 | head -1 || true)
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
echo -e "Harness: ${GREEN}${HARNESS_BIN}${NC}"
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
        "resolved_binary": "${PEER_RESOLVED}",
        "version": "${PEER_VERSION}"
    },
    "scenarios": []
}
EOF

TOTAL_SCENARIOS=0
PASSED_SCENARIOS=0
FAILED_SCENARIOS=0
SKIPPED_SCENARIOS=0

for scenario_file in "${SCENARIOS[@]}"; do
    scenario_name=$(basename "$scenario_file" .json)
    TOTAL_SCENARIOS=$((TOTAL_SCENARIOS + 1))

    echo -e "${CYAN}Running: ${scenario_name}${NC}"

    # Check if scenario exists
    if [[ ! -f "$scenario_file" ]]; then
        echo -e "${RED}  FAILED: Scenario file not found${NC}"
        FAILED_SCENARIOS=$((FAILED_SCENARIOS + 1))
        jq --arg name "$scenario_name" \
           --arg status "error" \
           --arg reason "Scenario file not found" \
           '.scenarios += [{"name": $name, "status": $status, "reason": $reason}]' \
           "$RESULTS_FILE" > "${RESULTS_FILE}.tmp" && mv "${RESULTS_FILE}.tmp" "$RESULTS_FILE"
        continue
    fi

    if [[ ! -x "$HARNESS_BIN" ]]; then
        echo -e "${RED}  FAILED: Interop harness not found or not executable${NC}"
        FAILED_SCENARIOS=$((FAILED_SCENARIOS + 1))
        jq --arg name "$scenario_name" \
           --arg status "error" \
           --arg reason "Interop harness not found" \
           --arg harness "$HARNESS_BIN" \
           '.scenarios += [{"name": $name, "status": $status, "reason": $reason, "harness": $harness}]' \
           "$RESULTS_FILE" > "${RESULTS_FILE}.tmp" && mv "${RESULTS_FILE}.tmp" "$RESULTS_FILE"
        continue
    fi

    # Check if peer binary exists
    if [[ -z "$PEER_RESOLVED" ]]; then
        echo -e "${RED}  FAILED: Peer binary not found or not executable${NC}"
        FAILED_SCENARIOS=$((FAILED_SCENARIOS + 1))
        jq --arg name "$scenario_name" \
           --arg status "error" \
           --arg reason "Peer binary not found or not executable" \
           --arg peer "$PEER_BIN" \
           '.scenarios += [{"name": $name, "status": $status, "reason": $reason, "peer": $peer}]' \
           "$RESULTS_FILE" > "${RESULTS_FILE}.tmp" && mv "${RESULTS_FILE}.tmp" "$RESULTS_FILE"
        continue
    fi

    export FLOWQ_INTEROP_PEER_BIN="$PEER_RESOLVED"
    export FLOWQ_INTEROP_SCENARIO="$scenario_name"

    start_ms=$(date +%s%3N)
    set +e
    harness_output=$("$HARNESS_BIN" "*${scenario_name}*" 2>&1)
    harness_exit=$?
    set -e
    end_ms=$(date +%s%3N)
    duration_ms=$((end_ms - start_ms))

    if [[ $harness_exit -eq 0 && "$harness_output" != *"SKIPPED:"* ]]; then
        echo -e "  ${GREEN}PASSED${NC}"
        PASSED_SCENARIOS=$((PASSED_SCENARIOS + 1))
        status="passed"
    elif [[ $harness_exit -eq 0 && "$harness_output" == *"SKIPPED:"* ]]; then
        echo -e "  ${YELLOW}SKIPPED: Harness reported scenario skip${NC}"
        SKIPPED_SCENARIOS=$((SKIPPED_SCENARIOS + 1))
        status="skipped"
    else
        echo -e "${RED}  FAILED: Harness exit code ${harness_exit}${NC}"
        FAILED_SCENARIOS=$((FAILED_SCENARIOS + 1))
        status="failed"
    fi

    jq --arg name "$scenario_name" \
       --arg status "$status" \
       --argjson duration_ms "$duration_ms" \
       --argjson harness_exit_code "$harness_exit" \
       --arg output "$harness_output" \
       '.scenarios += [{"name": $name, "status": $status, "duration_ms": $duration_ms, "harness_exit_code": $harness_exit_code, "output": $output}]' \
       "$RESULTS_FILE" > "${RESULTS_FILE}.tmp" && mv "${RESULTS_FILE}.tmp" "$RESULTS_FILE"
done

echo ""
echo -e "${CYAN}=== Summary ===${NC}"
echo "Total scenarios: $TOTAL_SCENARIOS"
echo "Passed: $PASSED_SCENARIOS"
echo "Failed: $FAILED_SCENARIOS"
echo "Skipped: $SKIPPED_SCENARIOS"
echo ""
echo "Results written to: $RESULTS_FILE"

# Exit with failure if any scenarios failed
if [[ $FAILED_SCENARIOS -gt 0 ]]; then
    exit 1
fi
