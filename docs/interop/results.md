# FlowQ Interop Results

## 2026-05-30 aioquic Handshake Evidence

- **FlowQ endpoint**: `build/windows-msvc-vcpkg-interop-openssl/Debug/flowq_quic_client.exe`
- **External peer**: Python `aioquic` 1.3.0 server from conda environment `expr`
- **Harness**: `tests/interop/test_interop.py`
- **Scope**: client Initial through TLS handshake confirmation against an external QUIC implementation.
- **Result**: pass. `aioquic` observed `HandshakeCompleted`; FlowQ reported `Handshake confirmed`.
- **Cleanup**: removed non-functional interop runner client/server placeholders and Docker interop scaffolding from the working tree. Future interop binaries must be backed by real QUIC behavior before being added.

## 2026-05-29 Local Harness Evidence

- **FlowQ branch**: `codex-production-quic-hardening`
- **Evidence type**: local script-to-harness wiring check from the working tree
- **Harness build**: `build/windows-msvc-vcpkg-interop/Debug/flowq_interop_tests.exe`
- **Scenario**: `basic_handshake`
- **Peer used for wiring check**: `C:\Windows\System32\where.exe`
- **Result file**: `build/interop-wiring.json` (ignored build artifact)
- **Outcome**: Pass for script-to-harness wiring. This is not external QUIC peer interoperability evidence.

## External QUIC Peer Availability

Checked on 2026-05-30:

- `ngtcp2*`: not found on PATH
- `quiche*`: not found on PATH
- `msquic*`: not found on PATH
- `picoquic*`: not found on PATH
- Docker CLI: present, Docker daemon running
- WSL: available for testing

## Open External Interop Results

- [x] `basic_handshake` against aioquic 1.3.0
- [ ] `stream_echo` against a named external QUIC peer and version
- [ ] `loss_recovery` against a named external QUIC peer and version

## Next Steps

1. Move server-chosen connection ID handling and coalesced packet processing fully into the core session/connection surfaces.
2. Add stream-level HQ or HTTP/3 exchange against aioquic.
3. Add a second external peer once locally available.
