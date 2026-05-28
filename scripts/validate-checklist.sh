#!/bin/bash
# validate-checklist.sh - Validate checklist items and forbidden wording
# Usage: ./scripts/validate-checklist.sh [--fix]
#
# This script checks for:
# - Forbidden wording (production-ready, RFC-compliant, secure, interoperable)
# - TODO/FIXME comments in production code paths
# - as any or type safety suppressions
# - Empty catch blocks
# Exit code 0 if all checks pass, 1 if any violations found.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

VIOLATIONS=0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "=== FlowQ Checklist Validator ==="
echo "Scanning: $REPO_ROOT"
echo ""

# 1. Check for forbidden wording in public-facing files
echo "Checking for forbidden wording..."

FORBIDDEN_WORDS=(
    "production-ready"
    "RFC-compliant"
    "interoperable"
    "secure"  # Only when claiming security, not when discussing security concepts
)

# Files to check (public-facing documentation)
DOC_FILES=(
    "README.md"
    "docs/README.md"
    "docs/production/release-checklist.md"
    "docs/production/readiness-gate.md"
    "docs/reference/architecture.md"
    "RELEASE_NOTES.md"
)

for word in "${FORBIDDEN_WORDS[@]}"; do
    for file in "${DOC_FILES[@]}"; do
        if [[ -f "$file" ]]; then
            matches=$(grep -n -i "$word" "$file" 2>/dev/null || true)
            if [[ -n "$matches" ]]; then
                echo -e "${YELLOW}WARNING${NC}: Found '${word}' in ${file}"
                echo "$matches" | head -5
                echo ""
            fi
        fi
    done
done

# 2. Check for TODO/FIXME in production code paths
echo ""
echo "Checking for TODO/FIXME in production code..."

TODO_COUNT=$(find include/ -name "*.hpp" -o -name "*.h" | xargs grep -r "TODO\|FIXME" 2>/dev/null | wc -l || echo "0")
if [[ $TODO_COUNT -gt 0 ]]; then
    echo -e "${YELLOW}WARNING${NC}: Found ${TODO_COUNT} TODO/FIXME comments in production headers"
    find include/ -name "*.hpp" -o -name "*.h" | xargs grep -r "TODO\|FIXME" 2>/dev/null | head -10
    echo ""
fi

# 3. Check for type safety suppressions
echo ""
echo "Checking for type safety suppressions..."

AS_ANY_COUNT=$(find include/ -name "*.hpp" -o -name "*.h" | xargs grep -r "as any\|@ts-ignore\|@ts-expect-error" 2>/dev/null | wc -l || echo "0")
if [[ $AS_ANY_COUNT -gt 0 ]]; then
    echo -e "${RED}VIOLATION${NC}: Found ${AS_ANY_COUNT} type safety suppressions"
    find include/ -name "*.hpp" -o -name "*.h" | xargs grep -r "as any\|@ts-ignore\|@ts-expect-error" 2>/dev/null
    VIOLATIONS=$((VIOLATIONS + AS_ANY_COUNT))
    echo ""
fi

# 4. Check for empty catch blocks
echo ""
echo "Checking for empty catch blocks..."

EMPTY_CATCH=$(find include/ -name "*.hpp" -o -name "*.h" | xargs grep -rP "catch\s*\([^)]*\)\s*\{\s*\}" 2>/dev/null | wc -l || echo "0")
if [[ $EMPTY_CATCH -gt 0 ]]; then
    echo -e "${RED}VIOLATION${NC}: Found ${EMPTY_CATCH} empty catch blocks"
    find include/ -name "*.hpp" -o -name "*.h" | xargs grep -rP "catch\s*\([^)]*\)\s*\{\s*\}" 2>/dev/null
    VIOLATIONS=$((VIOLATIONS + EMPTY_CATCH))
    echo ""
fi

# 5. Check for hardcoded credentials
echo ""
echo "Checking for hardcoded credentials..."

CRED_COUNT=$(find . -name "*.cpp" -o -name "*.hpp" -o -name "*.h" -o -name "*.json" -o -name "*.yml" -o -name "*.yaml" | \
    grep -v "node_modules\|build\|\.git" | \
    xargs grep -rP "(password|secret|token|key)\s*[:=]\s*['\"][^'\"]+['\"]" 2>/dev/null | wc -l || echo "0")
if [[ $CRED_COUNT -gt 0 ]]; then
    echo -e "${RED}VIOLATION${NC}: Found ${CRED_COUNT} potential hardcoded credentials"
    VIOLATIONS=$((VIOLATIONS + CRED_COUNT))
    echo ""
fi

echo ""
echo "=== Summary ==="
echo "Type safety suppressions: ${AS_ANY_COUNT}"
echo "Empty catch blocks: ${EMPTY_CATCH}"
echo "Potential hardcoded credentials: ${CRED_COUNT}"
echo "TODO/FIXME comments: ${TODO_COUNT}"

if [[ $VIOLATIONS -gt 0 ]]; then
    echo -e "${RED}FAILED${NC}: Found ${VIOLATIONS} violations"
    exit 1
else
    echo -e "${GREEN}PASSED${NC}: All checklist checks passed"
    exit 0
fi
