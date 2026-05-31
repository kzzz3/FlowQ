"""
FlowQ <-> quiche Interop Test

This module tests FlowQ against Cloudflare's quiche implementation.
"""

import asyncio
import os
import subprocess
import sys
from pathlib import Path


# quiche test binaries
QUICHE_CLIENT = os.environ.get("QUICHE_CLIENT", "quiche-client")
QUICHE_SERVER = os.environ.get("QUICHE_SERVER", "quiche-server")

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


def check_quiche_available() -> bool:
    """Check if quiche is available."""
    try:
        result = subprocess.run(
            [QUICHE_CLIENT, "--version"],
            capture_output=True,
            timeout=5,
        )
        return result.returncode == 0
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return False


async def test_handshake_flowq_server_quiche_client() -> tuple[bool, str]:
    """Test handshake: FlowQ server, quiche client."""
    print("  Testing: FlowQ server + quiche client")

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

        # Run quiche client
        client_proc = await asyncio.create_subprocess_exec(
            QUICHE_CLIENT,
            f"https://{SERVER_NAME}:4433/",
            "--no-verify",  # Self-signed cert
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
            if client_proc.returncode == 0 or "connection established" in output.lower():
                return True, "Handshake completed successfully"
            else:
                return False, f"Handshake failed: {output}"
        finally:
            client_proc.terminate()
            await client_proc.wait()
    finally:
        server_proc.terminate()
        await server_proc.wait()


async def test_handshake_quiche_server_flowq_client() -> tuple[bool, str]:
    """Test handshake: quiche server, FlowQ client."""
    print("  Testing: quiche server + FlowQ client")

    # Start quiche server
    server_proc = await asyncio.create_subprocess_exec(
        QUICHE_SERVER,
        f"--listen:{SERVER_NAME}:4434",
        f"--cert:{CERT_FILE}",
        f"--key:{KEY_FILE}",
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )

    try:
        # Wait for server to start
        await asyncio.sleep(1.0)

        # Run FlowQ client
        client_proc = await asyncio.create_subprocess_exec(
            str(FLOWQ_CLIENT),
            "--host", SERVER_NAME,
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


async def test_stream_echo_flowq_server_quiche_client() -> tuple[bool, str]:
    """Test stream echo: FlowQ server, quiche client."""
    print("  Testing: FlowQ server + quiche client (stream echo)")

    # Start FlowQ server
    server_proc = await asyncio.create_subprocess_exec(
        str(FLOWQ_SERVER),
        "--cert", str(CERT_FILE),
        "--key", str(KEY_FILE),
        "--port", "4433",
        "--echo",  Enable echo mode
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )

    try:
        # Wait for server to start
        await asyncio.sleep(1.0)

        # Run quiche client with data
        client_proc = await asyncio.create_subprocess_exec(
            QUICHE_CLIENT,
            f"https://{SERVER_NAME}:4433/",
            "--no-verify",
            "--data", "hello from quiche",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )

        try:
            stdout, stderr = await asyncio.wait_for(
                client_proc.communicate(),
                timeout=10.0,
            )

            # Check if echo succeeded
            output = stdout.decode()
            if "hello from quiche" in output:
                return True, "Stream echo completed successfully"
            else:
                return False, f"Stream echo failed: {output}"
        finally:
            client_proc.terminate()
            await client_proc.wait()
    finally:
        server_proc.terminate()
        await server_proc.wait()


async def run_quiche_tests() -> list[tuple[str, bool, str]]:
    """Run all quiche interop tests."""
    results = []

    if not check_quiche_available():
        print("quiche not available, skipping tests")
        return results

    # Test 1: FlowQ server + quiche client
    success, details = await test_handshake_flowq_server_quiche_client()
    results.append(("flowq_server_quiche_client", success, details))

    # Test 2: quiche server + FlowQ client
    success, details = await test_handshake_quiche_server_flowq_client()
    results.append(("quiche_server_flowq_client", success, details))

    # Test 3: Stream echo
    success, details = await test_stream_echo_flowq_server_quiche_client()
    results.append(("stream_echo_flowq_server_quiche_client", success, details))

    return results


def main():
    """Main entry point."""
    print("FlowQ <-> quiche Interop Tests")
    print("=" * 40)

    if not check_quiche_available():
        print("ERROR: quiche not found. Please install quiche.")
        print("Install: cargo install quiche")
        print("Or build from source: https://github.com/cloudflare/quiche")
        sys.exit(1)

    results = asyncio.run(run_quiche_tests())

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
