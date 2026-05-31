import asyncio
import datetime as dt
import ipaddress
import os
import sys
from enum import Enum
from pathlib import Path

from aioquic.asyncio.protocol import QuicConnectionProtocol
from aioquic.asyncio.server import serve
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import ConnectionTerminated, HandshakeCompleted, StreamDataReceived
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID


REPO_ROOT = Path(__file__).resolve().parents[2]
FLOWQ_CLIENT = Path(
    os.environ.get(
        "FLOWQ_CLIENT",
        REPO_ROOT / "build" / "windows-msvc-vcpkg-interop-openssl" / "Debug" / "flowq_quic_client.exe",
    )
)
CERT_FILE = REPO_ROOT / "build" / "certs" / "cert.pem"
KEY_FILE = REPO_ROOT / "build" / "certs" / "key.pem"
SERVER_NAME = "localhost"
EXPECTED_STREAM_DATA = b"hello from FlowQ"
EXPECTED_ECHO_DATA = b"echo from aioquic"


def ensure_test_certificate():
    CERT_FILE.parent.mkdir(parents=True, exist_ok=True)

    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    subject = issuer = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, SERVER_NAME)])
    now = dt.datetime.now(dt.UTC)

    cert = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(issuer)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - dt.timedelta(minutes=5))
        .not_valid_after(now + dt.timedelta(days=30))
        .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
        .add_extension(
            x509.KeyUsage(
                digital_signature=True,
                content_commitment=False,
                key_encipherment=True,
                data_encipherment=False,
                key_agreement=False,
                key_cert_sign=True,
                crl_sign=False,
                encipher_only=False,
                decipher_only=False,
            ),
            critical=True,
        )
        .add_extension(
            x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]),
            critical=False,
        )
        .add_extension(
            x509.SubjectAlternativeName(
                [
                    x509.DNSName(SERVER_NAME),
                    x509.IPAddress(ipaddress.ip_address("127.0.0.1")),
                ]
            ),
            critical=False,
        )
        .sign(key, hashes.SHA256())
    )

    CERT_FILE.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    KEY_FILE.write_bytes(
        key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.TraditionalOpenSSL,
            encryption_algorithm=serialization.NoEncryption(),
        )
    )


class Scenario(Enum):
    BIDIRECTIONAL_STREAM = "bidirectional_stream"
    LOSS_RECOVERY = "loss_recovery"


class RecordingProtocol(QuicConnectionProtocol):
    handshake_completed = None
    stream_data_received = None
    stream_payloads = None
    terminated = None
    scenario = Scenario.BIDIRECTIONAL_STREAM
    dropped_short_header_datagrams = 0

    def datagram_received(self, data, addr):
        if (
            self.scenario == Scenario.LOSS_RECOVERY
            and self.handshake_completed.is_set()
            and data
            and (data[0] & 0x80) == 0
            and type(self).dropped_short_header_datagrams == 0
        ):
            type(self).dropped_short_header_datagrams += 1
            print("[aioquic] intentionally dropped first short-header datagram")
            return
        super().datagram_received(data, addr)

    def quic_event_received(self, event):
        if isinstance(event, HandshakeCompleted):
            print("[aioquic] handshake completed")
            self.handshake_completed.set()
        elif isinstance(event, StreamDataReceived):
            print(f"[aioquic] stream data received: stream={event.stream_id} bytes={len(event.data)}")
            self.stream_payloads.append((event.stream_id, bytes(event.data)))
            if event.stream_id == 0 and bytes(event.data) == EXPECTED_STREAM_DATA:
                self._quic.send_stream_data(event.stream_id, EXPECTED_ECHO_DATA, end_stream=False)
                self.transmit()
                self.stream_data_received.set()
        elif isinstance(event, ConnectionTerminated):
            print(f"[aioquic] connection terminated: error={event.error_code} reason={event.reason_phrase}")
            self.terminated.set()


async def run_aioquic_server():
    config = QuicConfiguration(
        alpn_protocols=["hq-interop"],
        is_client=False,
    )
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
        env={
            **os.environ,
            "FLOWQ_QUIC_CA_FILE": str(CERT_FILE),
            "FLOWQ_QUIC_SERVER_NAME": SERVER_NAME,
        },
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
    scenario = Scenario(os.environ.get("FLOWQ_INTEROP_SCENARIO", Scenario.BIDIRECTIONAL_STREAM.value))
    print("=" * 50)
    print(f"FlowQ Interop Test: FlowQ Client -> aioquic Server ({scenario.value})")
    print("=" * 50)
    ensure_test_certificate()

    RecordingProtocol.handshake_completed = asyncio.Event()
    RecordingProtocol.stream_data_received = asyncio.Event()
    RecordingProtocol.stream_payloads = []
    RecordingProtocol.terminated = asyncio.Event()
    RecordingProtocol.scenario = scenario
    RecordingProtocol.dropped_short_header_datagrams = 0

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

        try:
            await asyncio.wait_for(RecordingProtocol.stream_data_received.wait(), timeout=5)
        except asyncio.TimeoutError:
            print(f"\n[Test] aioquic did not observe expected stream data: {EXPECTED_STREAM_DATA!r}")
            print(f"[Test] Observed stream payloads: {RecordingProtocol.stream_payloads!r}")
            return False
        if scenario == Scenario.LOSS_RECOVERY and RecordingProtocol.dropped_short_header_datagrams != 1:
            print("\n[Test] loss_recovery did not drop exactly one short-header datagram")
            return False

        print("\nInterop test PASSED: aioquic observed QUIC handshake, FlowQ STREAM data, and sent echo data")
        return True
    finally:
        server.close()


if __name__ == "__main__":
    result = asyncio.run(main())
    sys.exit(0 if result else 1)
