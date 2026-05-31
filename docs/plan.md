# FlowQ Current Production Plan

FlowQ is being hardened toward a narrow QUIC transport production-candidate boundary. This document is current-state planning only; release claims are controlled by `docs/production/readiness-gate.md`.

## Production-Candidate Boundary

In scope:

- QUIC v1 transport value codecs, packet-number helpers, frame/header codecs, and transport-parameter codec.
- Deterministic connection loop behavior for packet spaces, streams, flow control, ACK/loss, recovery timers, congestion accounting, lifecycle timers, routing, version negotiation, retry helpers, and endpoint lifecycle.
- OpenSSL-gated AES-128-GCM packet protection and RFC 9001 header protection when `FLOWQ_ENABLE_OPENSSL_CRYPTO=ON`.
- OpenSSL QUIC TLS adapter when `FLOWQ_ENABLE_OPENSSL_QUIC_TLS=ON`, including fail-closed server certificate/key configuration.
- Peer-issued connection ID migration policy: active CID limit enforcement, conflicting duplicate NEW_CONNECTION_ID rejection, retire_prior_to destination CID switching, and RETIRE_CONNECTION_ID emission.
- Inbound stateless reset handling for learned peer NEW_CONNECTION_ID tokens, including minimum-size enforcement and retired-token rejection.
- Public session, UDP/ASIO, endpoint-driver, timer scheduler, diagnostics, CMake package export, package-consumer, fuzz, and interop harness surfaces.
- aioquic external-peer evidence for handshake, bidirectional stream echo, and loss recovery.

Out of scope for the current production-candidate boundary:

- HTTP/3, QPACK, WebTransport, and 0-RTT deployment guarantees.
- Live AEAD key update installation.
- Stateless reset generation for locally issued connection IDs.
- AES-256-GCM and ChaCha20-Poly1305 packet protection.
- Security-audit, Linux, and sanitizer claims until evidence is recorded in the production gate.

## Active Gate Work

1. Keep installed headers limited to the production QUIC transport API.
2. Keep test-only plaintext protection out of installed public headers.
3. Keep external interop wrappers failing when requested scenarios are skipped.
4. Keep experimental examples outside the default build and install package.
5. Keep documentation synchronized with current code and evidence only.

## Verification Commands

Windows production gate:

```powershell
.\scripts\validate-build.ps1 -Preset windows-msvc-vcpkg -BuildType Debug
.\scripts\check-release-readiness.ps1 -SkipBuild
```

Linux/macOS release-readiness smoke gate:

```bash
./scripts/check-release-readiness.sh --skip-build
```

Strict production-candidate gate:

```powershell
.\scripts\check-release-readiness.ps1 -RequireCompleteReleaseChecklist
```

```bash
./scripts/check-release-readiness.sh --require-complete-release-checklist
```

The strict gate is expected to fail until Linux, sanitizer, and human review evidence is recorded and checked in `docs/production/release-checklist.md`.

Python aioquic interop from the `expr` conda environment:

```powershell
conda run -n expr python tests\interop\test_interop.py
$env:FLOWQ_INTEROP_SCENARIO = "loss_recovery"
conda run -n expr python tests\interop\test_interop.py
```

The direct Python aioquic harness supports `bidirectional_stream` (default) and `loss_recovery`; handshake completion is asserted in both scenarios. The C++ wrapper scenario files use `basic_handshake`, `stream_echo`, and `loss_recovery`.

Linux and sanitizer evidence must be generated on a Linux host using the presets and scripts documented in `docs/production/release-checklist.md`.
