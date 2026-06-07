#!/usr/bin/env python3
"""Validate human security review and external audit evidence.

The strict production gate must be backed by auditable files, not only checked
markdown boxes. This validator checks presence and minimum structured fields
without judging the security content itself.
"""

import argparse
import datetime as dt
import re
import sys
from pathlib import Path


REQUIRED_DOCUMENTS = (
    (
        Path("docs/security/reviews/human-security-review.md"),
        ("Reviewer", "Date", "Scope", "Commit", "Result", "Findings"),
    ),
    (
        Path("docs/security/audits/external-security-audit.md"),
        ("Auditor", "Date", "Scope", "Commit", "Result", "Findings"),
    ),
)

PLACEHOLDER_RE = re.compile(
    r"\b(TBD|TODO|FIXME|placeholder|example|sample|dummy|mock|lorem)\b",
    re.IGNORECASE,
)
COMMIT_RE = re.compile(r"^[0-9a-fA-F]{7,40}$")


def parse_args():
    parser = argparse.ArgumentParser(
        description="Validate strict-gate security review and audit evidence."
    )
    parser.add_argument(
        "--source-root",
        type=Path,
        default=Path.cwd(),
        help="Repository root to validate. Defaults to the current directory.",
    )
    return parser.parse_args()


def field_value(content, field):
    match = re.search(rf"(?im)^\s*{re.escape(field)}\s*:\s*(.+?)\s*$", content)
    if not match:
        return None
    value = match.group(1).strip()
    if not value:
        return None
    return value


def validate_document(source_root, relative_path, required_fields):
    issues = []
    path = source_root / relative_path
    if not path.is_file():
        return [f"Missing required security evidence: {relative_path.as_posix()}"]

    content = path.read_text(encoding="utf-8-sig")
    for field in required_fields:
        value = field_value(content, field)
        if value is None:
            issues.append(f"{relative_path.as_posix()} missing {field}: field")
            continue
        if PLACEHOLDER_RE.search(value):
            issues.append(
                f"{relative_path.as_posix()} contains placeholder {field}: {value}"
            )

    result = field_value(content, "Result")
    if result is not None and result.upper() not in {"PASS", "PASS WITH FINDINGS"}:
        issues.append(
            f"{relative_path.as_posix()} Result must be PASS or PASS WITH FINDINGS"
        )

    date = field_value(content, "Date")
    if date is not None:
        try:
            parsed_date = dt.date.fromisoformat(date)
        except ValueError:
            issues.append(f"{relative_path.as_posix()} Date must use YYYY-MM-DD")
        else:
            if parsed_date > dt.date.today():
                issues.append(
                    f"{relative_path.as_posix()} Date must not be in the future"
                )

    commit = field_value(content, "Commit")
    if commit is not None and not COMMIT_RE.fullmatch(commit):
        issues.append(
            f"{relative_path.as_posix()} Commit must be a 7-40 character hex SHA"
        )

    return issues


def main():
    args = parse_args()
    source_root = args.source_root.resolve()

    all_issues = []
    for relative_path, required_fields in REQUIRED_DOCUMENTS:
        all_issues.extend(validate_document(source_root, relative_path, required_fields))

    if all_issues:
        print("FAILED: security evidence validation failed")
        for issue in all_issues:
            print(f"  - {issue}")
        return 1

    print("PASSED: security evidence validation passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
