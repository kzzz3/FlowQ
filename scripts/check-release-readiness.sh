#!/bin/bash
# check-release-readiness.sh - Check release readiness on POSIX shells.
# Usage: ./scripts/check-release-readiness.sh [--skip-build] [--require-complete-release-checklist] [--source-root <path>]
#
# This script mirrors scripts/check-release-readiness.ps1 for Linux/macOS CI.
# Exit code 0 if all checks pass, 1 if any check fails.

set -e

SKIP_BUILD=0
REQUIRE_COMPLETE_RELEASE_CHECKLIST=0
SOURCE_ROOT=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-build)
            SKIP_BUILD=1
            shift
            ;;
        --require-complete-release-checklist)
            REQUIRE_COMPLETE_RELEASE_CHECKLIST=1
            shift
            ;;
        --source-root)
            if [[ $# -lt 2 ]]; then
                echo "FAILED: --source-root requires a path" >&2
                exit 1
            fi
            SOURCE_ROOT="$2"
            shift 2
            ;;
        --help|-h)
            echo "Usage: ./scripts/check-release-readiness.sh [--skip-build] [--require-complete-release-checklist] [--source-root <path>]"
            exit 0
            ;;
        *)
            echo "FAILED: unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

if [[ -n "$SOURCE_ROOT" ]]; then
    REPO_ROOT="$SOURCE_ROOT"
else
    REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
fi
cd "$REPO_ROOT"

FAILED=0
STEP_NUMBER=1

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}=== FlowQ Release Readiness Check ===${NC}"
echo ""

echo -e "${YELLOW}${STEP_NUMBER}. Checking documentation links...${NC}"
if ! ./scripts/validate-docs.sh; then
    FAILED=1
fi
STEP_NUMBER=$((STEP_NUMBER + 1))

echo ""
echo -e "${YELLOW}${STEP_NUMBER}. Checking checklist items...${NC}"
if ! ./scripts/validate-checklist.sh; then
    FAILED=1
fi
STEP_NUMBER=$((STEP_NUMBER + 1))

echo ""
echo -e "${YELLOW}${STEP_NUMBER}. Checking public packet protection API...${NC}"
PUBLIC_BYPASS_HITS="$(
    grep -RInE 'FLOWQ_ENABLE_TEST_PACKET_PROTECTION_BYPASS|packet_protection_policy|test_only' include/flowq/quic/*.hpp 2>/dev/null || true
)"
if [[ -n "$PUBLIC_BYPASS_HITS" ]]; then
    echo -e "${RED}FAILED: public QUIC headers must not expose packet protection bypass APIs:${NC}"
    echo "$PUBLIC_BYPASS_HITS"
    FAILED=1
fi
STEP_NUMBER=$((STEP_NUMBER + 1))

echo ""
echo -e "${YELLOW}${STEP_NUMBER}. Checking integration packet protection...${NC}"
INTEGRATION_BYPASS_HITS="$(
    grep -RInE 'plaintext_packet_protector|packet_protection_policy::test_allowed|\.protection_policy[[:space:]]*=' tests/integration/*.cpp 2>/dev/null || true
)"
if [[ -n "$INTEGRATION_BYPASS_HITS" ]]; then
    echo -e "${RED}FAILED: integration tests must exercise provider-backed packet protection instead of test bypasses:${NC}"
    echo "$INTEGRATION_BYPASS_HITS"
    FAILED=1
fi
STEP_NUMBER=$((STEP_NUMBER + 1))

if [[ $REQUIRE_COMPLETE_RELEASE_CHECKLIST -eq 1 ]]; then
    echo ""
    echo -e "${YELLOW}${STEP_NUMBER}. Checking release checklist completion...${NC}"
    RELEASE_CHECKLIST="docs/production/release-checklist.md"
    UNCHECKED_ITEMS="$(grep -nE '^[[:space:]]*-[[:space:]]+\[[[:space:]]\][[:space:]]+' "$RELEASE_CHECKLIST" 2>/dev/null || true)"
    if [[ -n "$UNCHECKED_ITEMS" ]]; then
        echo -e "${RED}FAILED: Release checklist has unchecked required items:${NC}"
        echo "$UNCHECKED_ITEMS"
        FAILED=1
    fi
    STEP_NUMBER=$((STEP_NUMBER + 1))
fi

echo ""
if [[ $SKIP_BUILD -eq 0 ]]; then
    echo -e "${YELLOW}${STEP_NUMBER}. Checking build...${NC}"
    if ! ./scripts/validate-build.sh --skip-tests; then
        FAILED=1
    fi
else
    echo -e "${YELLOW}${STEP_NUMBER}. Skipping build check (--skip-build)${NC}"
fi

echo ""
echo -e "${CYAN}=== Summary ===${NC}"
if [[ $FAILED -ne 0 ]]; then
    echo -e "${RED}FAILED: Release readiness check failed${NC}"
    exit 1
fi

echo -e "${GREEN}PASSED: Release readiness check passed${NC}"
