#!/bin/bash
# validate-checklist.sh - Validate checklist items and forbidden wording
# Usage: ./scripts/validate-checklist.sh [--fix]
#
# This script checks for:
# - Forbidden wording (production-ready, RFC-compliant, secure, interoperable)
# - TODO/FIXME comments in production code paths
# - as any or type safety suppressions
# - Empty catch blocks
# - Documentation comments on selected public QUIC APIs
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

is_policy_document() {
    [[ "$1" == "docs/production/release-checklist.md" || "$1" == "docs/production/readiness-gate.md" ]]
}

is_negated_forbidden_claim() {
    local line
    line="$(echo "$1" | tr '[:upper:]' '[:lower:]')"
    line="${line//\*/}"
    line="${line//_/}"
    [[ "$line" == *"not production-ready"* ]] ||
    [[ "$line" == *"not production quic"* ]] ||
    [[ "$line" =~ (^|[[:space:]])without[[:space:]]+claiming($|[[:space:]]) ]] ||
    [[ "$line" =~ (^|[[:space:]])explicitly[[:space:]]+deferred($|[[:space:]]) ]] ||
    [[ "$line" =~ (^|[[:space:]])no[[:space:]].*guarantees($|[[:space:]]) ]]
}

is_experimental_public_header() {
    case "$1" in
        include/flowq/quic/http3.hpp|\
        include/flowq/quic/http3_request.hpp|\
        include/flowq/quic/interop_runner.hpp|\
        include/flowq/quic/qpack.hpp|\
        include/flowq/quic/zero_rtt.hpp)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

DOCUMENTED_PUBLIC_API_HEADERS=(
    "include/flowq/quic/recovery_scheduler.hpp"
    "include/flowq/quic/lifecycle_scheduler.hpp"
    "include/flowq/quic/timer_scheduler.hpp"
)

find_public_api_doc_gaps() {
    awk '
        function is_public_decl(line) {
            return line ~ /^(enum class|struct|class)[[:space:]]+[A-Za-z_][A-Za-z0-9_]*/ ||
                   line ~ /^\[\[nodiscard\]\][[:space:]]+(inline[[:space:]]+)?(auto|[A-Za-z_:][A-Za-z0-9_:<>,&*[:space:]]*[[:space:]]+)[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\(/
        }

        function has_doc_comment(    idx,line) {
            for (idx = previous_count; idx >= 1; idx--) {
                line = previous[idx]
                sub(/^[[:space:]]+/, "", line)
                sub(/[[:space:]]+$/, "", line)
                if (line == "" || line ~ /^template[[:space:]]*</ || line ~ /^\[\[.*\]\]$/) {
                    continue
                }
                return line ~ /^(\/\/\/|\/\*\*)/
            }
            return 0
        }

        function remember(line,    idx) {
            previous_count++
            previous[previous_count] = line
            if (previous_count > 8) {
                for (idx = 1; idx < previous_count; idx++) {
                    previous[idx] = previous[idx + 1]
                }
                previous_count--
            }
        }

        {
            trimmed = $0
            sub(/^[[:space:]]+/, "", trimmed)
            sub(/[[:space:]]+$/, "", trimmed)

            if (trimmed == "namespace detail {") {
                in_detail = 1
                remember($0)
                next
            }
            if (trimmed == "} // namespace detail") {
                in_detail = 0
                remember($0)
                next
            }

            if (!in_detail && is_public_decl(trimmed) && !has_doc_comment()) {
                printf("%s:%d:%s\n", FILENAME, NR, trimmed)
            }
            remember($0)
        }
    ' "$1"
}

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

FORBIDDEN_CLAIM_COUNT=0
for word in "${FORBIDDEN_WORDS[@]}"; do
    for file in "${DOC_FILES[@]}"; do
        if [[ -f "$file" ]]; then
            while IFS= read -r match; do
                [[ -z "$match" ]] && continue
                line="${match#*:}"
                if is_policy_document "$file" || is_negated_forbidden_claim "$line"; then
                    continue
                fi

                echo -e "${RED}VIOLATION${NC}: Forbidden production claim '${word}' in ${file}"
                echo "$match"
                FORBIDDEN_CLAIM_COUNT=$((FORBIDDEN_CLAIM_COUNT + 1))
            done < <(grep -n -i "$word" "$file" 2>/dev/null || true)
        fi
    done
done

if [[ $FORBIDDEN_CLAIM_COUNT -gt 0 ]]; then
    VIOLATIONS=$((VIOLATIONS + FORBIDDEN_CLAIM_COUNT))
    echo ""
fi

# 2. Check for TODO/FIXME in production code paths
echo ""
echo "Checking for TODO/FIXME in production code..."

TODO_COUNT=$(find include/ -name "*.hpp" -o -name "*.h" | xargs grep -r "TODO\|FIXME" 2>/dev/null | wc -l || echo "0")
if [[ $TODO_COUNT -gt 0 ]]; then
    echo -e "${RED}VIOLATION${NC}: Found ${TODO_COUNT} TODO/FIXME comments in production headers"
    find include/ -name "*.hpp" -o -name "*.h" | xargs grep -r "TODO\|FIXME" 2>/dev/null | head -10
    VIOLATIONS=$((VIOLATIONS + TODO_COUNT))
    echo ""
fi

# 3. Check for implementation placeholders in production public headers
echo ""
echo "Checking for placeholder implementations in production headers..."

PLACEHOLDER_COUNT=0
PLACEHOLDER_PATTERNS=(
    "simulate successful"
    "not implemented"
    "For now"
    "stub implementation"
    "Deterministic stub"
    "always succeeds"
)

while IFS= read -r file; do
    is_experimental_public_header "$file" && continue

    for pattern in "${PLACEHOLDER_PATTERNS[@]}"; do
        matches=$(grep -n -i "$pattern" "$file" 2>/dev/null || true)
        if [[ -n "$matches" ]]; then
            count=$(echo "$matches" | wc -l)
            PLACEHOLDER_COUNT=$((PLACEHOLDER_COUNT + count))
            echo -e "${RED}VIOLATION${NC}: Found placeholder wording '${pattern}' in ${file}"
            echo "$matches" | head -5
        fi
    done
done < <(find include/ -name "*.hpp" -o -name "*.h")

if [[ $PLACEHOLDER_COUNT -gt 0 ]]; then
    VIOLATIONS=$((VIOLATIONS + PLACEHOLDER_COUNT))
    echo ""
fi

# 4. Check for type safety suppressions
echo ""
echo "Checking for type safety suppressions..."

AS_ANY_COUNT=$(find include/ -name "*.hpp" -o -name "*.h" | xargs grep -r "as any\|@ts-ignore\|@ts-expect-error" 2>/dev/null | wc -l || echo "0")
if [[ $AS_ANY_COUNT -gt 0 ]]; then
    echo -e "${RED}VIOLATION${NC}: Found ${AS_ANY_COUNT} type safety suppressions"
    find include/ -name "*.hpp" -o -name "*.h" | xargs grep -r "as any\|@ts-ignore\|@ts-expect-error" 2>/dev/null
    VIOLATIONS=$((VIOLATIONS + AS_ANY_COUNT))
    echo ""
fi

# 5. Check for empty catch blocks
echo ""
echo "Checking for empty catch blocks..."

EMPTY_CATCH=$(find include/ -name "*.hpp" -o -name "*.h" | xargs grep -rP "catch\s*\([^)]*\)\s*\{\s*\}" 2>/dev/null | wc -l || echo "0")
if [[ $EMPTY_CATCH -gt 0 ]]; then
    echo -e "${RED}VIOLATION${NC}: Found ${EMPTY_CATCH} empty catch blocks"
    find include/ -name "*.hpp" -o -name "*.h" | xargs grep -rP "catch\s*\([^)]*\)\s*\{\s*\}" 2>/dev/null
    VIOLATIONS=$((VIOLATIONS + EMPTY_CATCH))
    echo ""
fi

# 6. Check for hardcoded credentials
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

# 7. Check for public API documentation comments on selected production QUIC headers
echo ""
echo "Checking public QUIC API documentation comments..."

PUBLIC_API_DOC_GAP_COUNT=0
for file in "${DOCUMENTED_PUBLIC_API_HEADERS[@]}"; do
    [[ -f "$file" ]] || continue
    matches=$(find_public_api_doc_gaps "$file")
    if [[ -n "$matches" ]]; then
        count=$(printf "%s\n" "$matches" | wc -l)
        PUBLIC_API_DOC_GAP_COUNT=$((PUBLIC_API_DOC_GAP_COUNT + count))
        while IFS= read -r match; do
            [[ -z "$match" ]] && continue
            echo -e "${RED}VIOLATION${NC}: Public API declaration lacks documentation in ${match}"
        done <<< "$matches"
    fi
done

if [[ $PUBLIC_API_DOC_GAP_COUNT -gt 0 ]]; then
    VIOLATIONS=$((VIOLATIONS + PUBLIC_API_DOC_GAP_COUNT))
    echo ""
fi

echo ""
echo "=== Summary ==="
echo "Type safety suppressions: ${AS_ANY_COUNT}"
echo "Empty catch blocks: ${EMPTY_CATCH}"
echo "Potential hardcoded credentials: ${CRED_COUNT}"
echo "TODO/FIXME comments: ${TODO_COUNT}"
echo "Forbidden production claims: ${FORBIDDEN_CLAIM_COUNT}"
echo "Placeholder implementation wording: ${PLACEHOLDER_COUNT}"
echo "Public API documentation gaps: ${PUBLIC_API_DOC_GAP_COUNT}"

if [[ $VIOLATIONS -gt 0 ]]; then
    echo -e "${RED}FAILED${NC}: Found ${VIOLATIONS} violations"
    exit 1
else
    echo -e "${GREEN}PASSED${NC}: All checklist checks passed"
    exit 0
fi
