import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
VALIDATOR = REPO_ROOT / "scripts" / "validate-interop-evidence.py"


def write_report(directory, name, peer_name, scenarios):
    report = {
        "peer": {"name": peer_name, "version": "1.0.0"},
        "summary": {
            "total": len(scenarios),
            "passed": sum(1 for scenario in scenarios if scenario["status"] == "passed"),
            "failed": sum(1 for scenario in scenarios if scenario["status"] != "passed"),
        },
        "scenarios": scenarios,
        "metadata": {
            "timestamp": "2026-06-06T00:00:00Z",
            "flowq_commit": "abcdef0",
        },
    }
    path = directory / name
    path.write_text(json.dumps(report), encoding="utf-8")
    return path


def passed_scenario(name):
    return {
        "name": name,
        "status": "passed",
        "exit_code": 0,
        "duration_ms": 10,
        "output": "scenario passed",
    }


def run_validator(results_dir, *extra_args):
    return subprocess.run(
        [
            sys.executable,
            str(VALIDATOR),
            "--results-dir",
            str(results_dir),
            *extra_args,
        ],
        cwd=REPO_ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=15,
        check=False,
    )


class InteropEvidenceValidatorTests(unittest.TestCase):
    def test_accepts_one_full_flow_peer_with_required_scenarios(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            results_dir = Path(temp_dir)
            write_report(
                results_dir,
                "aioquic.json",
                "aioquic",
                [passed_scenario("bidirectional_stream"), passed_scenario("loss_recovery")],
            )

            result = run_validator(results_dir, "--min-full-flow-peers", "1")

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("PASSED", result.stdout)
        self.assertIn("aioquic", result.stdout)

    def test_strict_peer_count_fails_closed_with_only_one_full_flow_peer(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            results_dir = Path(temp_dir)
            write_report(
                results_dir,
                "aioquic.json",
                "aioquic",
                [passed_scenario("bidirectional_stream"), passed_scenario("loss_recovery")],
            )

            result = run_validator(results_dir, "--min-full-flow-peers", "2")

        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("Need at least 2 distinct full-flow peer", result.stdout)
        self.assertIn("found 1", result.stdout)

    def test_rejects_smoke_only_report_as_full_flow_evidence(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            results_dir = Path(temp_dir)
            write_report(
                results_dir,
                "ngtcp2-smoke.json",
                "ngtcp2",
                [passed_scenario("initial_packet_smoke")],
            )

            result = run_validator(results_dir, "--min-full-flow-peers", "1")

        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("missing required scenario", result.stdout)
        self.assertIn("bidirectional_stream", result.stdout)
        self.assertIn("loss_recovery", result.stdout)

    def test_rejects_failed_required_scenario(self):
        failed_loss_recovery = passed_scenario("loss_recovery")
        failed_loss_recovery["status"] = "failed"
        failed_loss_recovery["exit_code"] = 23
        failed_loss_recovery["output"] = "loss recovery failed"

        with tempfile.TemporaryDirectory() as temp_dir:
            results_dir = Path(temp_dir)
            write_report(
                results_dir,
                "aioquic.json",
                "aioquic",
                [passed_scenario("bidirectional_stream"), failed_loss_recovery],
            )

            result = run_validator(results_dir, "--min-full-flow-peers", "1")

        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("scenario loss_recovery did not pass", result.stdout)

    def test_rejects_report_missing_required_metadata(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            results_dir = Path(temp_dir)
            report_path = write_report(
                results_dir,
                "aioquic.json",
                "aioquic",
                [passed_scenario("bidirectional_stream"), passed_scenario("loss_recovery")],
            )
            payload = json.loads(report_path.read_text(encoding="utf-8"))
            payload["metadata"].pop("flowq_commit")
            report_path.write_text(json.dumps(payload), encoding="utf-8")

            result = run_validator(results_dir, "--min-full-flow-peers", "1")

        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("missing metadata.flowq_commit", result.stdout)

    def test_rejects_mismatched_summary_counts(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            results_dir = Path(temp_dir)
            report_path = write_report(
                results_dir,
                "aioquic.json",
                "aioquic",
                [passed_scenario("bidirectional_stream"), passed_scenario("loss_recovery")],
            )
            payload = json.loads(report_path.read_text(encoding="utf-8"))
            payload["summary"]["passed"] = 1
            report_path.write_text(json.dumps(payload), encoding="utf-8")

            result = run_validator(results_dir, "--min-full-flow-peers", "1")

        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("summary.passed does not match", result.stdout)


if __name__ == "__main__":
    unittest.main()
