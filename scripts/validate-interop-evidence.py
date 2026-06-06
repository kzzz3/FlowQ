#!/usr/bin/env python3
"""Validate checked-in external QUIC interop evidence."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Optional


DEFAULT_REQUIRED_SCENARIOS = ("bidirectional_stream", "loss_recovery")


def scenario_passed(scenario: dict) -> bool:
    return scenario.get("status") == "passed" and scenario.get("exit_code") == 0


def validate_report(path: Path, required_scenarios: tuple[str, ...]) -> tuple[Optional[str], bool, list[str]]:
    issues: list[str] = []

    try:
        report = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as error:
        return None, False, [f"{path}: unable to read JSON report: {error}"]

    peer = report.get("peer") if isinstance(report, dict) else None
    peer_name = peer.get("name") if isinstance(peer, dict) else None
    peer_version = peer.get("version") if isinstance(peer, dict) else None
    if not isinstance(peer_name, str) or not peer_name.strip():
        issues.append(f"{path}: missing peer.name")
        peer_name = None
    elif peer_name != peer_name.strip():
        peer_name = peer_name.strip()
    if not isinstance(peer_version, str) or not peer_version.strip():
        issues.append(f"{path}: missing peer.version")

    scenarios = report.get("scenarios") if isinstance(report, dict) else None
    if not isinstance(scenarios, list) or not scenarios:
        issues.append(f"{path}: missing scenarios")
        return peer_name, False, issues

    named_scenarios = {
        scenario.get("name"): scenario
        for scenario in scenarios
        if isinstance(scenario, dict) and isinstance(scenario.get("name"), str)
    }

    missing = [name for name in required_scenarios if name not in named_scenarios]
    if missing:
        issues.append(
            f"{path}: peer {peer_name or '<unknown>'} missing required scenario(s): "
            + ", ".join(missing)
        )

    for name in required_scenarios:
        scenario = named_scenarios.get(name)
        if scenario is None:
            continue
        if not scenario_passed(scenario):
            issues.append(
                f"{path}: peer {peer_name or '<unknown>'} scenario {name} did not pass "
                f"(status={scenario.get('status')!r}, exit_code={scenario.get('exit_code')!r})"
            )

    summary = report.get("summary") if isinstance(report, dict) else None
    if not isinstance(summary, dict):
        issues.append(f"{path}: missing summary")
    else:
        expected_total = len(scenarios)
        expected_passed = sum(1 for scenario in scenarios if isinstance(scenario, dict) and scenario_passed(scenario))
        expected_failed = expected_total - expected_passed
        if summary.get("total") != expected_total:
            issues.append(f"{path}: summary.total does not match scenario count")
        if summary.get("passed") != expected_passed:
            issues.append(f"{path}: summary.passed does not match passed scenario count")
        if summary.get("failed") != expected_failed:
            issues.append(f"{path}: summary.failed does not match failed scenario count")
        if expected_failed != 0:
            issues.append(f"{path}: report contains {expected_failed} failed scenario(s)")

    metadata = report.get("metadata") if isinstance(report, dict) else None
    if not isinstance(metadata, dict):
        issues.append(f"{path}: missing metadata")
    else:
        for field in ("timestamp", "flowq_commit"):
            value = metadata.get(field)
            if not isinstance(value, str) or not value.strip():
                issues.append(f"{path}: missing metadata.{field}")

    full_flow = peer_name is not None and not issues
    return peer_name, full_flow, issues


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--results-dir",
        default="docs/interop/results",
        type=Path,
        help="Directory containing machine-readable interop JSON reports.",
    )
    parser.add_argument(
        "--min-full-flow-peers",
        default=1,
        type=int,
        help="Minimum number of distinct peers with all required scenarios passing.",
    )
    parser.add_argument(
        "--required-scenario",
        action="append",
        dest="required_scenarios",
        help="Scenario that must pass for a peer to count as full-flow evidence.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    required_scenarios = tuple(args.required_scenarios or DEFAULT_REQUIRED_SCENARIOS)

    if args.min_full_flow_peers < 1:
        print("FAILED: --min-full-flow-peers must be at least 1")
        return 1

    if not args.results_dir.is_dir():
        print(f"FAILED: interop results directory not found: {args.results_dir}")
        return 1

    reports = sorted(args.results_dir.glob("*.json"))
    if not reports:
        print(f"FAILED: no interop evidence JSON reports found in {args.results_dir}")
        return 1

    peer_status: dict[str, bool] = {}
    peer_issues: dict[str, list[str]] = {}
    general_issues: list[str] = []

    for path in reports:
        peer_name, full_flow, issues = validate_report(path, required_scenarios)
        if peer_name is None:
            general_issues.extend(issues)
            continue

        peer_status[peer_name] = peer_status.get(peer_name, False) or full_flow
        if issues:
            peer_issues.setdefault(peer_name, []).extend(issues)
            general_issues.extend(issues)

    full_flow_peers = sorted(peer for peer, has_full_flow in peer_status.items() if has_full_flow)
    if len(full_flow_peers) < args.min_full_flow_peers:
        general_issues.append(
            f"Need at least {args.min_full_flow_peers} distinct full-flow peer(s); "
            f"found {len(full_flow_peers)}"
        )
        for peer, has_full_flow in sorted(peer_status.items()):
            if not has_full_flow and peer not in peer_issues:
                general_issues.append(f"Peer {peer} has no full-flow evidence")

    if general_issues:
        print("FAILED: Interop evidence validation failed")
        for issue in general_issues:
            print(f"  - {issue}")
        return 1

    print(
        "PASSED: Interop evidence has "
        f"{len(full_flow_peers)} distinct full-flow peer(s): {', '.join(full_flow_peers)}"
    )
    print("Required scenarios: " + ", ".join(required_scenarios))
    return 0


if __name__ == "__main__":
    sys.exit(main())
