import asyncio
import os
import ssl
import sys
from pathlib import Path

from aioquic.asyncio.protocol import QuicConnectionProtocol
from aioquic.asyncio.server import serve
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import ConnectionTerminated, HandshakeCompleted, StreamDataReceived


REPO_ROOT = Path(__file__).resolve().parents[2]
FLOWQ_CLIENT = Path(
    os.environ.get(
        "FLOWQ_CLIENT",
        REPO_ROOT / "build" / "windows-msvc-vcpkg-interop-openssl" / "Debug" / "flowq_quic_client.exe",
    )
)
CERT_FILE = REPO_ROOT / "build" / "certs" / "cert.pem"
KEY_FILE = REPO_ROOT / "build" / "certs" / "key.pem"


class RecordingProtocol(QuicConnectionProtocol):
    handshake_completed = None
    stream_data_received = None
    terminated = None

    def quic_event_received(self, event):
        if isinstance(event, HandshakeCompleted):
            print("[aioquic] handshake completed")
            self.handshake_completed.set()
        elif isinstance(event, StreamDataReceived):
            print(f"[aioquic] stream data received: stream={event.stream_id} bytes={len(event.data)}")
            self.stream_data_received.set()
        elif isinstance(event, ConnectionTerminated):
            print(f"[aioquic] connection terminated: error={event.error_code} reason={event.reason_phrase}")
            self.terminated.set()


async def run_aioquic_server():
    config = QuicConfiguration(
        alpn_protocols=["hq-interop"],
        is_client=False,
    )
    config.verify_mode = ssl.CERT_NONE
    config.load_cert_chain(str(CERT_FILE), str(KEY_FILE))

    return await serve(
        "127.0.0.1",
        4433,
        configuration=config,
        create_protocol=RecordingProtocol,
    )


async def run_flowq_client():
    print("[Test] Starting FlowQ client...")
    if not FLOWQ_CLIENT.exists():
        raise FileNotFoundError(f"FlowQ client binary not found: {FLOWQ_CLIENT}")

    proc = await asyncio.create_subprocess_exec(
        str(FLOWQ_CLIENT),
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )

    try:
        stdout, stderr = await asyncio.wait_for(proc.communicate(), timeout=10)
        print(f"[Test] Client stdout: {stdout.decode()}")
        if stderr:
            print(f"[Test] Client stderr: {stderr.decode()}")
        return proc.returncode
    except asyncio.TimeoutError:
        print("[Test] Client timed out")
        proc.kill()
        await proc.wait()
        return -1


async def main():
    print("=" * 50)
    print("FlowQ Interop Test: FlowQ Client -> aioquic Server")
    print("=" * 50)

    RecordingProtocol.handshake_completed = asyncio.Event()
    RecordingProtocol.stream_data_received = asyncio.Event()
    RecordingProtocol.terminated = asyncio.Event()

    server = await run_aioquic_server()

    try:
        client_exit = await run_flowq_client()
        if client_exit != 0:
            print(f"\n[Test] FlowQ client exited with {client_exit}")
            return False

        try:
            await asyncio.wait_for(RecordingProtocol.handshake_completed.wait(), timeout=5)
        except asyncio.TimeoutError:
            print("\n[Test] aioquic did not observe QUIC HandshakeCompleted")
            return False

        print("\nInterop test PASSED: aioquic observed QUIC handshake completion")
        return True
    finally:
        server.close()


if __name__ == "__main__":
    result = asyncio.run(main())
    sys.exit(0 if result else 1)
