# FlowQ Interop Results

## 2026-05-29 Local Harness Evidence

- **FlowQ branch**: `codex-production-quic-hardening`
- **Evidence type**: local script-to-harness wiring check from the working tree
- **Harness build**: `build/windows-msvc-vcpkg-interop/Debug/flowq_interop_tests.exe`
- **Scenario**: `basic_handshake`
- **Peer used for wiring check**: `C:\Windows\System32\where.exe`
- **Result file**: `build/interop-wiring.json` (ignored build artifact)
- **Outcome**: Pass for script-to-harness wiring. This is not external QUIC peer interoperability evidence.

## External QUIC Peer Availability

Checked on 2026-05-29:

- `ngtcp2*`: not found on PATH
- `quiche*`: not found on PATH
- `msquic*`: not found on PATH
- `picoquic*`: not found on PATH
- Docker CLI: present, but Docker daemon was not running
- WSL: no available Linux distribution

## Open External Interop Results

- [ ] `basic_handshake` against a named external QUIC peer and version
- [ ] `stream_echo` against a named external QUIC peer and version
- [ ] `loss_recovery` against a named external QUIC peer and version
