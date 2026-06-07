import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
VALIDATOR = REPO_ROOT / "scripts" / "validate-security-evidence.py"


def run_validator(source_root):
    return subprocess.run(
        [
            sys.executable,
            str(VALIDATOR),
            "--source-root",
            str(source_root),
        ],
        cwd=REPO_ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=15,
        check=False,
    )


def write_required_evidence(source_root):
    review = source_root / "docs" / "security" / "reviews" / "human-security-review.md"
    audit = source_root / "docs" / "security" / "audits" / "external-security-audit.md"
    review.parent.mkdir(parents=True, exist_ok=True)
    audit.parent.mkdir(parents=True, exist_ok=True)
    review.write_text(
        "\n".join(
            [
                "# Human Security Review",
                "",
                "Reviewer: Jane Chen",
                "Date: 2026-06-07",
                "Scope: FlowQ QUIC transport production readiness.",
                "Commit: abcdef0",
                "Result: PASS",
                "Findings: No open critical or high findings.",
                "",
            ]
        ),
        encoding="utf-8",
    )
    audit.write_text(
        "\n".join(
            [
                "# External Security Audit",
                "",
                "Auditor: Acme Security LLC",
                "Date: 2026-06-07",
                "Scope: FlowQ QUIC transport production readiness.",
                "Commit: abcdef0",
                "Result: PASS",
                "Findings: No open critical or high findings.",
                "",
            ]
        ),
        encoding="utf-8",
    )


class SecurityEvidenceValidatorTests(unittest.TestCase):
    def test_fails_closed_when_required_security_evidence_is_missing(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            result = run_validator(Path(temp_dir))

        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("human-security-review.md", result.stdout)
        self.assertIn("external-security-audit.md", result.stdout)

    def test_accepts_required_security_evidence_with_auditable_fields(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source_root = Path(temp_dir)
            write_required_evidence(source_root)

            result = run_validator(source_root)

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("PASSED", result.stdout)

    def test_rejects_placeholder_security_evidence(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source_root = Path(temp_dir)
            write_required_evidence(source_root)
            review = source_root / "docs" / "security" / "reviews" / "human-security-review.md"
            review.write_text(
                "\n".join(
                    [
                        "# Human Security Review",
                        "",
                        "Reviewer: TBD",
                        "Date: 2026-06-07",
                        "Scope: FlowQ QUIC transport production readiness.",
                        "Commit: abcdef0",
                        "Result: PASS",
                        "Findings: No open critical or high findings.",
                        "",
                    ]
                ),
                encoding="utf-8",
            )

            result = run_validator(source_root)

        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("placeholder", result.stdout)
        self.assertIn("Reviewer", result.stdout)

    def test_rejects_non_auditable_date_and_commit_fields(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source_root = Path(temp_dir)
            write_required_evidence(source_root)
            audit = source_root / "docs" / "security" / "audits" / "external-security-audit.md"
            audit.write_text(
                "\n".join(
                    [
                        "# External Security Audit",
                        "",
                        "Auditor: Acme Security LLC",
                        "Date: next week",
                        "Scope: FlowQ QUIC transport production readiness.",
                        "Commit: release-candidate",
                        "Result: PASS",
                        "Findings: No open critical or high findings.",
                        "",
                    ]
                ),
                encoding="utf-8",
            )

            result = run_validator(source_root)

        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("Date", result.stdout)
        self.assertIn("Commit", result.stdout)


if __name__ == "__main__":
    unittest.main()
