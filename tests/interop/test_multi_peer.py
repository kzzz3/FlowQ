"""
FlowQ Multi-Peer Interop Test Framework

This module provides a unified framework for testing FlowQ against multiple
QUIC implementations (peers).

Supported peers:
- aioquic (Python)
- MsQuic (C/C++)
- quiche (Rust)
- ngtcp2 (C)
- picoquic (C)
"""

import asyncio
import json
import os
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple


class PeerType(Enum):
    """Supported QUIC peer implementations."""
    AIOQUIC = "aioquic"
    MSQUIC = "msquic"
    QUICHE = "quiche"
    NGTCP2 = "ngtcp2"
    PICOQUIC = "picoquic"


class Scenario(Enum):
    """Test scenarios for interop testing."""
    BASIC_HANDSHAKE = "basic_handshake"
    STREAM_ECHO = "stream_echo"
    LOSS_RECOVERY = "loss_recovery"
    KEY_UPDATE = "key_update"


class TestResult(Enum):
    """Test result status."""
    PASS = "pass"
    FAIL = "fail"
    ERROR = "error"
    SKIP = "skip"


@dataclass
class PeerConfig:
    """Configuration for a QUIC peer."""
    peer_type: PeerType
    binary_path: Path
    server_args: List[str] = field(default_factory=list)
    client_args: List[str] = field(default_factory=list)
    env_vars: Dict[str, str] = field(default_factory=dict)
    version: str = "unknown"


@dataclass
class ScenarioResult:
    """Result of a single scenario execution."""
    peer: PeerType
    scenario: Scenario
    result: TestResult
    duration_ms: float
    details: str = ""
    error: str = ""


@dataclass
class InteropReport:
    """Complete interop test report."""
    date: str
    flowq_commit: str
    flowq_version: str
    platform: str
    results: List[ScenarioResult] = field(default_factory=list)

    def to_dict(self) -> dict:
        return {
            "date": self.date,
            "flowq_commit": self.flowq_commit,
            "flowq_version": self.flowq_version,
            "platform": self.platform,
            "scenarios": [
                {
                    "peer": r.peer.value,
                    "scenario": r.scenario.value,
                    "result": r.result.value,
                    "duration_ms": r.duration_ms,
                    "details": r.details,
                    "error": r.error,
                }
                for r in self.results
            ],
            "summary": {
                "total": len(self.results),
                "passed": sum(1 for r in self.results if r.result == TestResult.PASS),
                "failed": sum(1 for r in self.results if r.result == TestResult.FAIL),
                "errors": sum(1 for r in self.results if r.result == TestResult.ERROR),
                "skipped": sum(1 for r in self.results if r.result == TestResult.SKIP),
            },
        }


class PeerDiscovery:
    """Discovers available QUIC peer implementations."""

    @staticmethod
    def discover_peers() -> Dict[PeerType, PeerConfig]:
        """Discover all available peer implementations."""
        peers = {}

        # Try aioquic
        aioquic_path = PeerDiscovery._find_aioquic()
        if aioquic_path:
            peers[PeerType.AIOQUIC] = aioquic_path

        # Try MsQuic
        msquic_path = PeerDiscovery._find_msquic()
        if msquic_path:
            peers[PeerType.MSQUIC] = msquic_path

        # Try quiche
        quiche_path = PeerDiscovery._find_quiche()
        if quiche_path:
            peers[PeerType.QUICHE] = quiche_path

        # Try ngtcp2
        ngtcp2_path = PeerDiscovery._find_ngtcp2()
        if ngtcp2_path:
            peers[PeerType.NGTCP2] = ngtcp2_path

        # Try picoquic
        picoquic_path = PeerDiscovery._find_picoquic()
        if picoquic_path:
            peers[PeerType.PICOQUIC] = picoquic_path

        return peers

    @staticmethod
    def _find_aioquic() -> Optional[PeerConfig]:
        """Find aioquic installation."""
        try:
            import aioquic
            version = aioquic.__version__
            # aioquic is a Python library, we'll use it directly
            return PeerConfig(
                peer_type=PeerType.AIOQUIC,
                binary_path=Path(sys.executable),  # Python interpreter
                version=version,
            )
        except ImportError:
            return None

    @staticmethod
    def _find_msquic() -> Optional[PeerConfig]:
        """Find MsQuic binary."""
        # Check common locations
        search_paths = [
            Path(os.environ.get("MSQUIC_PATH", "")),
            Path("C:/msquic/bin"),
            Path.home() / "msquic" / "bin",
            Path("tools/msquic"),
        ]

        # Also check PATH
        for name in ["msquic.exe", "quicping.exe", "quicsample.exe"]:
            path = PeerDiscovery._which(name)
            if path:
                search_paths.append(path.parent)

        for base_path in search_paths:
            if not base_path.exists():
                continue

            # Look for known MsQuic binaries
            for binary in ["quicping.exe", "quicsample.exe", "msquic.exe"]:
                binary_path = base_path / binary
                if binary_path.exists():
                    version = PeerDiscovery._get_msquic_version(binary_path)
                    return PeerConfig(
                        peer_type=PeerType.MSQUIC,
                        binary_path=binary_path,
                        version=version,
                    )

        return None

    @staticmethod
    def _find_quiche() -> Optional[PeerConfig]:
        """Find quiche binary."""
        # Check if cargo is available
        cargo_path = PeerDiscovery._which("cargo")
        if not cargo_path:
            return None

        # Check for pre-built quiche
        search_paths = [
            Path("tools/quiche/target/release"),
            Path("tools/quiche/target/debug"),
            Path.home() / ".cargo" / "bin",
        ]

        for base_path in search_paths:
            for binary in ["quiche-client", "quiche-server", "quiche.exe"]:
                binary_path = base_path / binary
                if binary_path.exists():
                    return PeerConfig(
                        peer_type=PeerType.QUICHE,
                        binary_path=binary_path,
                        version="unknown",
                    )

        return None

    @staticmethod
    def _find_ngtcp2() -> Optional[PeerConfig]:
        """Find ngtcp2 binary."""
        search_paths = [
            Path("tools/ngtcp2/build"),
            Path("tools/ngtcp2"),
        ]

        for base_path in search_paths:
            for binary in ["ngtcp2-client", "ngtcp2-server", "client.exe", "server.exe"]:
                binary_path = base_path / binary
                if binary_path.exists():
                    return PeerConfig(
                        peer_type=PeerType.NGTCP2,
                        binary_path=binary_path,
                        version="unknown",
                    )

        return None

    @staticmethod
    def _find_picoquic() -> Optional[PeerConfig]:
        """Find picoquic binary."""
        search_paths = [
            Path("tools/picoquic"),
            Path("tools/picoquic/build"),
        ]

        for base_path in search_paths:
            for binary in ["picoquicclient", "picoquicserver", "picoquicclient.exe", "picoquicserver.exe"]:
                binary_path = base_path / binary
                if binary_path.exists():
                    return PeerConfig(
                        peer_type=PeerType.PICOQUIC,
                        binary_path=binary_path,
                        version="unknown",
                    )

        return None

    @staticmethod
    def _which(name: str) -> Optional[Path]:
        """Find executable in PATH."""
        import shutil
        path = shutil.which(name)
        return Path(path) if path else None

    @staticmethod
    def _get_msquic_version(binary_path: Path) -> str:
        """Get MsQuic version from binary."""
        try:
            result = subprocess.run(
                [str(binary_path), "--version"],
                capture_output=True,
                text=True,
                timeout=5,
            )
            if result.returncode == 0:
                return result.stdout.strip()
        except Exception:
            pass
        return "unknown"


class ScenarioRunner:
    """Runs interop test scenarios."""

    def __init__(
        self,
        flowq_client: Path,
        flowq_server: Path,
        cert_file: Path,
        key_file: Path,
        server_name: str = "localhost",
    ):
        self.flowq_client = flowq_client
        self.flowq_server = flowq_server
        self.cert_file = cert_file
        self.key_file = key_file
        self.server_name = server_name

    async def run_scenario(
        self,
        peer_config: PeerConfig,
        scenario: Scenario,
        timeout_seconds: float = 30.0,
    ) -> ScenarioResult:
        """Run a single scenario against a peer."""
        import time
        start = time.time()

        try:
            if peer_config.peer_type == PeerType.AIOQUIC:
                result = await self._run_aioquic_scenario(peer_config, scenario, timeout_seconds)
            else:
                result = await self._run_generic_scenario(peer_config, scenario, timeout_seconds)

            duration_ms = (time.time() - start) * 1000
            result.duration_ms = duration_ms
            return result

        except asyncio.TimeoutError:
            duration_ms = (time.time() - start) * 1000
            return ScenarioResult(
                peer=peer_config.peer_type,
                scenario=scenario,
                result=TestResult.ERROR,
                duration_ms=duration_ms,
                error="Test timed out",
            )
        except Exception as e:
            duration_ms = (time.time() - start) * 1000
            return ScenarioResult(
                peer=peer_config.peer_type,
                scenario=scenario,
                result=TestResult.ERROR,
                duration_ms=duration_ms,
                error=str(e),
            )

    async def _run_aioquic_scenario(
        self,
        peer_config: PeerConfig,
        scenario: Scenario,
        timeout_seconds: float,
    ) -> ScenarioResult:
        """Run scenario using aioquic."""
        # Import aioquic-specific test runner
        from test_interop import run_aioquic_test

        success, details = await run_aioquic_test(
            scenario=scenario.value,
            cert_file=self.cert_file,
            key_file=self.key_file,
            server_name=self.server_name,
            timeout=timeout_seconds,
        )

        return ScenarioResult(
            peer=PeerType.AIOQUIC,
            scenario=scenario,
            result=TestResult.PASS if success else TestResult.FAIL,
            duration_ms=0,  # Will be set by caller
            details=details,
        )

    async def _run_generic_scenario(
        self,
        peer_config: PeerConfig,
        scenario: Scenario,
        timeout_seconds: float,
    ) -> ScenarioResult:
        """Run scenario using generic peer binary."""
        # This is a placeholder for generic peer integration
        # Each peer would have its own implementation

        if scenario == Scenario.BASIC_HANDSHAKE:
            return await self._run_handshake_scenario(peer_config, timeout_seconds)
        elif scenario == Scenario.STREAM_ECHO:
            return await self._run_stream_echo_scenario(peer_config, timeout_seconds)
        elif scenario == Scenario.LOSS_RECOVERY:
            return await self._run_loss_recovery_scenario(peer_config, timeout_seconds)
        else:
            return ScenarioResult(
                peer=peer_config.peer_type,
                scenario=scenario,
                result=TestResult.SKIP,
                duration_ms=0,
                details=f"Scenario {scenario.value} not implemented for {peer_config.peer_type.value}",
            )

    async def _run_handshake_scenario(
        self,
        peer_config: PeerConfig,
        timeout_seconds: float,
    ) -> ScenarioResult:
        """Run handshake scenario."""
        # Start FlowQ server
        server_proc = await asyncio.create_subprocess_exec(
            str(self.flowq_server),
            "--cert", str(self.cert_file),
            "--key", str(self.key_file),
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )

        try:
            # Wait for server to start
            await asyncio.sleep(0.5)

            # Run peer client
            client_proc = await asyncio.create_subprocess_exec(
                str(peer_config.binary_path),
                *peer_config.client_args,
                "--host", self.server_name,
                "--port", "4433",
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )

            try:
                stdout, stderr = await asyncio.wait_for(
                    client_proc.communicate(),
                    timeout=timeout_seconds,
                )

                if client_proc.returncode == 0:
                    return ScenarioResult(
                        peer=peer_config.peer_type,
                        scenario=Scenario.BASIC_HANDSHAKE,
                        result=TestResult.PASS,
                        duration_ms=0,
                        details=stdout.decode() if stdout else "",
                    )
                else:
                    return ScenarioResult(
                        peer=peer_config.peer_type,
                        scenario=Scenario.BASIC_HANDSHAKE,
                        result=TestResult.FAIL,
                        duration_ms=0,
                        error=stderr.decode() if stderr else "Client exited with non-zero code",
                    )
            finally:
                client_proc.terminate()
                await client_proc.wait()
        finally:
            server_proc.terminate()
            await server_proc.wait()

    async def _run_stream_echo_scenario(
        self,
        peer_config: PeerConfig,
        timeout_seconds: float,
    ) -> ScenarioResult:
        """Run stream echo scenario."""
        # Similar to handshake but with data exchange
        return ScenarioResult(
            peer=peer_config.peer_type,
            scenario=Scenario.STREAM_ECHO,
            result=TestResult.SKIP,
            duration_ms=0,
            details="Stream echo not yet implemented for generic peers",
        )

    async def _run_loss_recovery_scenario(
        self,
        peer_config: PeerConfig,
        timeout_seconds: float,
    ) -> ScenarioResult:
        """Run loss recovery scenario."""
        return ScenarioResult(
            peer=peer_config.peer_type,
            scenario=Scenario.LOSS_RECOVERY,
            result=TestResult.SKIP,
            duration_ms=0,
            details="Loss recovery not yet implemented for generic peers",
        )


async def run_multi_peer_interop(
    flowq_client: Path,
    flowq_server: Path,
    cert_file: Path,
    key_file: Path,
    output_dir: Path,
    scenarios: Optional[List[Scenario]] = None,
) -> InteropReport:
    """Run interop tests against all available peers."""
    import datetime as dt
    import subprocess

    # Get FlowQ commit
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            capture_output=True,
            text=True,
            cwd=flowq_client.parent.parent,
        )
        flowq_commit = result.stdout.strip() if result.returncode == 0 else "unknown"
    except Exception:
        flowq_commit = "unknown"

    # Create report
    report = InteropReport(
        date=dt.datetime.now().strftime("%Y-%m-%d"),
        flowq_commit=flowq_commit,
        flowq_version="1.0.0",
        platform=sys.platform,
    )

    # Discover peers
    peers = PeerDiscovery.discover_peers()
    print(f"Discovered {len(peers)} peer(s): {', '.join(p.value for p in peers.keys())}")

    if not peers:
        print("No peers found. Skipping interop tests.")
        return report

    # Create scenario runner
    runner = ScenarioRunner(
        flowq_client=flowq_client,
        flowq_server=flowq_server,
        cert_file=cert_file,
        key_file=key_file,
    )

    # Run scenarios
    if scenarios is None:
        scenarios = [Scenario.BASIC_HANDSHAKE, Scenario.STREAM_ECHO, Scenario.LOSS_RECOVERY]

    for peer_type, peer_config in peers.items():
        print(f"\nTesting against {peer_type.value} ({peer_config.version})...")

        for scenario in scenarios:
            print(f"  Running {scenario.value}...", end=" ")
            result = await runner.run_scenario(peer_config, scenario)
            report.results.append(result)

            if result.result == TestResult.PASS:
                print("PASS")
            elif result.result == TestResult.FAIL:
                print(f"FAIL: {result.error}")
            elif result.result == TestResult.ERROR:
                print(f"ERROR: {result.error}")
            else:
                print("SKIP")

    # Save report
    output_dir.mkdir(parents=True, exist_ok=True)
    report_file = output_dir / f"interop-{report.date}.json"
    with open(report_file, "w") as f:
        json.dump(report.to_dict(), f, indent=2)

    print(f"\nReport saved to: {report_file}")
    return report


def main():
    """Main entry point."""
    import argparse

    parser = argparse.ArgumentParser(description="FlowQ Multi-Peer Interop Test")
    parser.add_argument("--flowq-client", type=Path, required=True, help="Path to FlowQ client binary")
    parser.add_argument("--flowq-server", type=Path, required=True, help="Path to FlowQ server binary")
    parser.add_argument("--cert", type=Path, required=True, help="Path to TLS certificate")
    parser.add_argument("--key", type=Path, required=True, help="Path to TLS key")
    parser.add_argument("--output", type=Path, default=Path("docs/benchmarks/results"), help="Output directory")
    parser.add_argument("--scenarios", nargs="+", choices=[s.value for s in Scenario], help="Scenarios to run")

    args = parser.parse_args()

    scenarios = [Scenario(s) for s in args.scenarios] if args.scenarios else None

    report = asyncio.run(run_multi_peer_interop(
        flowq_client=args.flowq_client,
        flowq_server=args.flowq_server,
        cert_file=args.cert,
        key_file=args.key,
        output_dir=args.output,
        scenarios=scenarios,
    ))

    # Print summary
    summary = report.to_dict()["summary"]
    print(f"\nSummary:")
    print(f"  Total: {summary['total']}")
    print(f"  Passed: {summary['passed']}")
    print(f"  Failed: {summary['failed']}")
    print(f"  Errors: {summary['errors']}")
    print(f"  Skipped: {summary['skipped']}")

    sys.exit(0 if summary["failed"] == 0 and summary["errors"] == 0 else 1)


if __name__ == "__main__":
    main()
