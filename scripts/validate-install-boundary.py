#!/usr/bin/env python3
"""Validate that source-only QUIC headers are excluded from installation."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


SOURCE_ONLY_HEADERS = (
    "http3.hpp",
    "http3_request.hpp",
    "qpack.hpp",
    "zero_rtt.hpp",
)


def strip_comments(text: str) -> str:
    lines = []
    for line in text.splitlines():
        lines.append(line.split("#", 1)[0])
    return "\n".join(lines)


def extract_install_blocks(cmake_text: str) -> list[str]:
    text = strip_comments(cmake_text)
    blocks: list[str] = []
    for match in re.finditer(r"\binstall\s*\(", text, flags=re.IGNORECASE):
        start = match.start()
        depth = 0
        for index in range(match.end() - 1, len(text)):
            char = text[index]
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if depth == 0:
                    blocks.append(text[start : index + 1])
                    break
    return blocks


def normalise_cmake(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def install_block_installs_include(block: str) -> bool:
    normalised = normalise_cmake(block).lower()
    return bool(re.search(r"\bdirectory\s+include/?\b", normalised))


def validate_install_boundary(source_root: Path) -> list[str]:
    issues: list[str] = []
    cmake_file = source_root / "CMakeLists.txt"
    if not cmake_file.is_file():
        return [f"missing top-level CMakeLists.txt: {cmake_file}"]

    for header in SOURCE_ONLY_HEADERS:
        header_path = source_root / "include" / "flowq" / "quic" / header
        if not header_path.is_file():
            issues.append(f"missing source-only header expected by install boundary: {header_path}")

    install_blocks = [
        block for block in extract_install_blocks(cmake_file.read_text(encoding="utf-8")) if install_block_installs_include(block)
    ]
    if not install_blocks:
        issues.append("missing install(DIRECTORY include/ ...) rule")
        return issues

    for block in install_blocks:
        normalised = normalise_cmake(block)
        if "FILES_MATCHING" not in normalised:
            issues.append("install(DIRECTORY include/ ...) must use FILES_MATCHING")
        for header in SOURCE_ONLY_HEADERS:
            pattern = rf'PATTERN\s+"{re.escape(header)}"\s+EXCLUDE\b'
            if not re.search(pattern, normalised):
                issues.append(f'install(DIRECTORY include/ ...) must exclude source-only header: {header}')

    return issues


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()

    source_root = args.source_root.resolve()
    issues = validate_install_boundary(source_root)
    if issues:
        print("FAILED: install boundary validation failed")
        for issue in issues:
            print(f"  - {issue}")
        return 1

    print("PASSED: install boundary validation passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
