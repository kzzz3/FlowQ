import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
VALIDATOR = REPO_ROOT / "scripts" / "validate-install-boundary.py"
SOURCE_ONLY_HEADERS = ("http3.hpp", "http3_request.hpp", "qpack.hpp", "zero_rtt.hpp")


def run_validator(source_root: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(VALIDATOR), "--source-root", str(source_root)],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def write_source_tree(root: Path, cmake: str) -> None:
    include_dir = root / "include" / "flowq" / "quic"
    include_dir.mkdir(parents=True)
    for header in SOURCE_ONLY_HEADERS:
        (include_dir / header).write_text("// source-only header\n", encoding="utf-8")
    (root / "CMakeLists.txt").write_text(textwrap.dedent(cmake), encoding="utf-8")


class InstallBoundaryValidatorTests(unittest.TestCase):
    def test_accepts_install_rule_that_excludes_source_only_headers(self) -> None:
        result = run_validator(REPO_ROOT)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("PASSED", result.stdout)

    def test_rejects_install_rule_missing_source_only_excludes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            source_root = Path(tmp)
            write_source_tree(
                source_root,
                """
                install(
                    DIRECTORY include/
                    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
                    FILES_MATCHING
                        PATTERN "*.hpp"
                        PATTERN "http3.hpp" EXCLUDE
                )
                """,
            )

            result = run_validator(source_root)

        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("http3_request.hpp", result.stdout)
        self.assertIn("qpack.hpp", result.stdout)
        self.assertIn("zero_rtt.hpp", result.stdout)

    def test_rejects_unfiltered_include_directory_install(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            source_root = Path(tmp)
            write_source_tree(
                source_root,
                """
                install(
                    DIRECTORY include/
                    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
                )
                """,
            )

            result = run_validator(source_root)

        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("FILES_MATCHING", result.stdout)


if __name__ == "__main__":
    unittest.main()
