import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
IMPORTER = REPO_ROOT / "scripts" / "import-interop-evidence.py"


def passed_scenario(name):
    return {
        "name": name,
        "status": "passed",
        "exit_code": 0,
        "duration_ms": 10,
        "output": "scenario passed",
    }


def write_report(path, peer_name="quiche", timestamp="2026-06-07T12:34:56Z", commit="abcdef0"):
    report = {
        "peer": {"name": peer_name, "version": "1.0.0"},
        "summary": {"total": 2, "passed": 2, "failed": 0},
        "scenarios": [passed_scenario("bidirectional_stream"), passed_scenario("loss_recovery")],
        "metadata": {
            "timestamp": timestamp,
            "flowq_commit": commit,
        },
    }
    path.write_text(json.dumps(report), encoding="utf-8")
    return path


def run_importer(source_report, results_dir, *extra_args):
    return subprocess.run(
        [
            sys.executable,
            str(IMPORTER),
            str(source_report),
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


class InteropEvidenceImporterTests(unittest.TestCase):
    def test_imports_valid_external_report_with_canonical_name(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = write_report(root / "external-report.json")
            results_dir = root / "results"

            result = run_importer(source, results_dir)

            expected_path = results_dir / "quiche-20260607T123456Z-abcdef0.json"
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertTrue(expected_path.exists(), result.stdout)
            self.assertIn(str(expected_path), result.stdout)
            imported = json.loads(expected_path.read_text(encoding="utf-8"))
            self.assertEqual(imported["peer"]["name"], "quiche")

    def test_rejects_invalid_report_without_writing_output(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = write_report(root / "external-report.json")
            payload = json.loads(source.read_text(encoding="utf-8"))
            payload["scenarios"] = [passed_scenario("initial_packet_smoke")]
            payload["summary"] = {"total": 1, "passed": 1, "failed": 0}
            source.write_text(json.dumps(payload), encoding="utf-8")
            results_dir = root / "results"

            result = run_importer(source, results_dir)

            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn("missing required scenario", result.stdout)
            self.assertFalse(results_dir.exists())

    def test_accepts_utf8_sig_report(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = write_report(root / "external-report.json")
            source.write_text(source.read_text(encoding="utf-8"), encoding="utf-8-sig")
            results_dir = root / "results"

            result = run_importer(source, results_dir)

            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertTrue((results_dir / "quiche-20260607T123456Z-abcdef0.json").exists())

    def test_refuses_to_overwrite_existing_report_without_force(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = write_report(root / "external-report.json")
            results_dir = root / "results"

            first = run_importer(source, results_dir)
            second = run_importer(source, results_dir)

            self.assertEqual(first.returncode, 0, first.stdout)
            self.assertNotEqual(second.returncode, 0, second.stdout)
            self.assertIn("already exists", second.stdout)

    def test_force_allows_replacing_existing_report(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = write_report(root / "external-report.json")
            results_dir = root / "results"

            first = run_importer(source, results_dir)
            second = run_importer(source, results_dir, "--force")

            self.assertEqual(first.returncode, 0, first.stdout)
            self.assertEqual(second.returncode, 0, second.stdout)


if __name__ == "__main__":
    unittest.main()
