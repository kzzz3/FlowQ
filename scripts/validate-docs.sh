#!/bin/bash
# validate-docs.sh - Validate documentation links and references
# Usage: ./scripts/validate-docs.sh [--fix]
#
# This script scans all .md files for internal links and verifies each linked file exists.
# It reports broken links with file:line references.
# Exit code 0 if all links are valid, 1 if any broken links found.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

BROKEN_LINKS=0
TOTAL_LINKS=0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "=== FlowQ Documentation Link Validator ==="
echo "Scanning: $REPO_ROOT"
echo ""

# Find all markdown files
find . -name "*.md" -type f | while read -r md_file; do
    # Skip node_modules and build directories
    if [[ "$md_file" == *"/node_modules/"* ]] || [[ "$md_file" == *"/build/"* ]]; then
        continue
    fi

    # Extract all markdown links [text](url)
    grep -n '\[.*\](.*\.md)' "$md_file" 2>/dev/null | while IFS=: read -r line_num link_line; do
        # Extract all links from the line
        echo "$link_line" | grep -oP '\[.*?\]\(\K[^)]+\.md' | while read -r link; do
            TOTAL_LINKS=$((TOTAL_LINKS + 1))

            # Resolve relative path
            if [[ "$link" == /* ]]; then
                # Absolute path from repo root
                target_file=".${link}"
            else
                # Relative path from current file's directory
                dir=$(dirname "$md_file")
                target_file="${dir}/${link}"
            fi

            # Normalize path (resolve .. and .)
            target_file=$(realpath -m "$target_file" 2>/dev/null || echo "$target_file")

            # Check if file exists
            if [[ ! -f "$target_file" ]]; then
                echo -e "${RED}BROKEN${NC}: ${md_file}:${line_num}"
                echo -e "  Link: ${link}"
                echo -e "  Expected: ${target_file}"
                echo ""
                BROKEN_LINKS=$((BROKEN_LINKS + 1))
            fi
        done
    done
done

echo ""
echo "=== Summary ==="
echo "Total links scanned: ${TOTAL_LINKS}"
echo "Broken links found: ${BROKEN_LINKS}"

if [[ $BROKEN_LINKS -gt 0 ]]; then
    echo -e "${RED}FAILED${NC}: Documentation has broken links"
    exit 1
else
    echo -e "${GREEN}PASSED${NC}: All documentation links are valid"
    exit 0
fi
