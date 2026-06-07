#!/usr/bin/env python3
"""Import a pre-recorded external QUIC interop evidence report."""

import argparse
import importlib.util
import json
import re
import shutil
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
VALIDATOR = REPO_ROOT / "scripts" / "validate-interop-evidence.py"


def load_validator():
    spec = importlib.util.spec_from_file_location("validate_interop_evidence", VALIDATOR)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"could not load validator from {VALIDATOR}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def slug(value):
    normalized = re.sub(r"[^A-Za-z0-9._-]+", "-", value.strip().lower())
    normalized = normalized.strip("-._")
    return normalized or "unknown-peer"


def compact_timestamp(value):
    compact = re.sub(r"[-:]", "", value.strip())
    compact = compact.replace("+0000", "Z")
    compact = compact.replace(".000", "")
    compact = compact.replace(".", "")
    return slug(compact).upper()


def short_commit(value):
    cleaned = re.sub(r"[^A-Fa-f0-9]", "", value.strip())
    return cleaned[:12] or "unknown"


def canonical_name(report):
    peer = report.get("peer", {})
    metadata = report.get("metadata", {})
    peer_name = slug(str(peer.get("name", "unknown-peer")))
    timestamp = compact_timestamp(str(metadata.get("timestamp", "unknown-time")))
    commit = short_commit(str(metadata.get("flowq_commit", "unknown")))
    return f"{peer_name}-{timestamp}-{commit}.json"


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Validate and import an already-recorded external QUIC interop JSON report "
            "into docs/interop/results without running a peer."
        )
    )
    parser.add_argument("report", type=Path, help="Path to the pre-recorded JSON report.")
    parser.add_argument(
        "--results-dir",
        type=Path,
        default=REPO_ROOT / "docs" / "interop" / "results",
        help="Directory where checked-in interop evidence reports are stored.",
    )
    parser.add_argument("--force", action="store_true", help="Replace an existing canonical report file.")
    return parser.parse_args()


def main():
    args = parse_args()
    source = args.report
    if not source.is_file():
        print(f"FAILED: report does not exist: {source}", file=sys.stderr)
        return 1

    try:
        report = json.loads(source.read_text(encoding="utf-8-sig"))
    except json.JSONDecodeError as exc:
        print(f"FAILED: invalid JSON in {source}: {exc}", file=sys.stderr)
        return 1

    validator = load_validator()
    peer_name, full_flow, issues = validator.validate_report(source, validator.DEFAULT_REQUIRED_SCENARIOS)
    if peer_name is None or not full_flow or issues:
        for error in issues:
            print(f"FAILED: {error}", file=sys.stderr)
        return 1

    target = args.results_dir / canonical_name(report)
    if target.exists() and not args.force:
        print(f"FAILED: target report already exists: {target}", file=sys.stderr)
        return 1

    args.results_dir.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    print(f"PASSED: imported interop evidence report: {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
