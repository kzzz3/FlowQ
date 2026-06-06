import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = REPO_ROOT / "scripts" / "run-aioquic-interop.ps1"


def run_runner(*args, env=None):
    return subprocess.run(
        [
            "powershell.exe",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(SCRIPT),
            *args,
        ],
        cwd=REPO_ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=30,
        check=False,
        env=env,
    )


class AioquicRunnerScriptTests(unittest.TestCase):
    def test_runner_fails_closed_when_flowq_client_is_missing(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            result = run_runner(
                "-BuildDir",
                temp_dir,
                "-CondaEnv",
                "expr",
                "-Scenario",
                "bidirectional_stream",
                "-OutputDir",
                temp_dir,
            )

        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("FlowQ client binary not found", result.stdout)

    def test_runner_rejects_unsupported_aioquic_scenario_names(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            result = run_runner(
                "-BuildDir",
                temp_dir,
                "-Scenario",
                "unsupported_scenario",
                "-OutputDir",
                temp_dir,
            )

        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("unsupported_scenario", result.stdout)

    def test_runner_writes_report_when_aioquic_scenario_fails(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            build_dir = root / "build"
            client_dir = build_dir / "Debug"
            client_dir.mkdir(parents=True)
            (client_dir / "flowq_quic_client.exe").write_text("", encoding="utf-8")

            fake_bin = root / "bin"
            fake_bin.mkdir()
            (fake_bin / "conda.cmd").write_text(
                "\r\n".join(
                    [
                        "@echo off",
                        'if "%1"=="env" if "%2"=="list" (',
                        "  echo expr * C:\\fake\\expr",
                        "  exit /b 0",
                        ")",
                        'if "%1"=="run" if "%2"=="-n" if "%4"=="python" if "%5"=="-c" (',
                        "  echo 1.3.0",
                        "  exit /b 0",
                        ")",
                        "echo simulated scenario failure 1>&2",
                        "exit /b 23",
                        "",
                    ]
                ),
                encoding="utf-8",
            )

            output_dir = root / "results"
            env = os.environ.copy()
            env["PATH"] = str(fake_bin) + os.pathsep + env.get("PATH", "")

            result = run_runner(
                "-BuildDir",
                str(build_dir),
                "-OutputDir",
                str(output_dir),
                "-CondaEnv",
                "expr",
                "-Scenario",
                "bidirectional_stream",
                env=env,
            )

            reports = list(output_dir.glob("aioquic-*.json"))
            report = json.loads(reports[0].read_text(encoding="utf-8-sig")) if reports else None

        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("simulated scenario failure", result.stdout)
        self.assertEqual(len(reports), 1, result.stdout)

        self.assertIsNotNone(report)
        self.assertEqual(report["summary"]["failed"], 1)
        self.assertEqual(report["scenarios"][0]["name"], "bidirectional_stream")
        self.assertEqual(report["scenarios"][0]["exit_code"], 23)
        self.assertIn("simulated scenario failure", report["scenarios"][0]["output"])


if __name__ == "__main__":
    unittest.main()
