"""
FlowQ <-> MsQuic Interop Test

This module tests FlowQ against Microsoft's MsQuic implementation.
"""

import asyncio
import os
import subprocess
import sys
from pathlib import Path


# MsQuic test binaries
MSQUIC_CLIENT = os.environ.get("MSQUIC_CLIENT", "quicping.exe")
MSQUIC_SERVER = os.environ.get("MSQUIC_SERVER", "quicping.exe")

# FlowQ binaries
REPO_ROOT = Path(__file__).resolve().parents[2]
FLOWQ_CLIENT = Path(
    os.environ.get(
        "FLOWQ_CLIENT",
        REPO_ROOT / "build" / "windows-msvc-vcpkg-interop-openssl" / "Debug" / "flowq_quic_client.exe",
    )
)
FLOWQ_SERVER = Path(
    os.environ.get(
        "FLOWQ_SERVER",
        REPO_ROOT / "build" / "windows-msvc-vcpkg-interop-openssl" / "Debug" / "flowq_quic_server.exe",
    )
)

# TLS configuration
CERT_FILE = REPO_ROOT / "build" / "certs" / "cert.pem"
KEY_FILE = REPO_ROOT / "build" / "certs" / "key.pem"
SERVER_NAME = "localhost"


def check_msquic_available() -> bool:
    """Check if MsQuic is available."""
    try:
        result = subprocess.run(
            [MSQUIC_CLIENT, "--help"],
            capture_output=True,
            timeout=5,
        )
        return result.returncode == 0
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return False


async def test_handshake_flowq_server_msquic_client() -> tuple[bool, str]:
    """Test handshake: FlowQ server, MsQuic client."""
    print("  Testing: FlowQ server + MsQuic client")

    # Start FlowQ server
    server_proc = await asyncio.create_subprocess_exec(
        str(FLOWQ_SERVER),
        "--cert", str(CERT_FILE),
        "--key", str(KEY_FILE),
        "--port", "4433",
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )

    try:
        # Wait for server to start
        await asyncio.sleep(1.0)

        # Run MsQuic client
        client_proc = await asyncio.create_subprocess_exec(
            MSQUIC_CLIENT,
            "-server:localhost",
            "-port:4433",
            "-unsafe:selfsigned",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )

        try:
            stdout, stderr = await asyncio.wait_for(
                client_proc.communicate(),
                timeout=10.0,
            )

            # Check if handshake succeeded
            output = stdout.decode() + stderr.decode()
            if "Handshake complete" in output or client_proc.returncode == 0:
                return True, "Handshake completed successfully"
            else:
                return False, f"Handshake failed: {output}"
        finally:
            client_proc.terminate()
            await client_proc.wait()
    finally:
        server_proc.terminate()
        await server_proc.wait()


async def test_handshake_msquic_server_flowq_client() -> tuple[bool, str]:
    """Test handshake: MsQuic server, FlowQ client."""
    print("  Testing: MsQuic server + FlowQ client")

    # Start MsQuic server
    server_proc = await asyncio.create_subprocess_exec(
        MSQUIC_SERVER,
        "-server:localhost",
        "-port:4434",
        "-cert:" + str(CERT_FILE),
        "-key:" + str(KEY_FILE),
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )

    try:
        # Wait for server to start
        await asyncio.sleep(1.0)

        # Run FlowQ client
        client_proc = await asyncio.create_subprocess_exec(
            str(FLOWQ_CLIENT),
            "--host", "localhost",
            "--port", "4434",
            "--ca", str(CERT_FILE),
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )

        try:
            stdout, stderr = await asyncio.wait_for(
                client_proc.communicate(),
                timeout=10.0,
            )

            # Check if handshake succeeded
            output = stdout.decode() + stderr.decode()
            if "Handshake confirmed" in output or "Connected" in output:
                return True, "Handshake completed successfully"
            else:
                return False, f"Handshake failed: {output}"
        finally:
            client_proc.terminate()
            await client_proc.wait()
    finally:
        server_proc.terminate()
        await server_proc.wait()


async def run_msquic_tests() -> list[tuple[str, bool, str]]:
    """Run all MsQuic interop tests."""
    results = []

    if not check_msquic_available():
        print("MsQuic not available, skipping tests")
        return results

    # Test 1: FlowQ server + MsQuic client
    success, details = await test_handshake_flowq_server_msquic_client()
    results.append(("flowq_server_msquic_client", success, details))

    # Test 2: MsQuic server + FlowQ client
    success, details = await test_handshake_msquic_server_flowq_client()
    results.append(("msquic_server_flowq_client", success, details))

    return results


def main():
    """Main entry point."""
    print("FlowQ <-> MsQuic Interop Tests")
    print("=" * 40)

    if not check_msquic_available():
        print("ERROR: MsQuic not found. Please install MsQuic and add to PATH.")
        print("Download: https://github.com/microsoft/msquic/releases")
        sys.exit(1)

    results = asyncio.run(run_msquic_tests())

    print("\nResults:")
    print("-" * 40)
    for name, success, details in results:
        status = "PASS" if success else "FAIL"
        print(f"  {name}: {status}")
        if not success:
            print(f"    {details}")

    passed = sum(1 for _, success, _ in results if success)
    total = len(results)
    print(f"\n{passed}/{total} tests passed")

    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()
