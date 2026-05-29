# FlowQ Production Readiness Gate

This document records the current evidence required before FlowQ can claim production-candidate status.

## Current Status

- **Level**: Production-readiness gate
- **Date**: 2026-05-29
- **Status**: Non-production. The codebase has local build/test evidence, OpenSSL-gated AES-128-GCM packet protection, and deterministic transport behavior. External peer QUIC interop evidence and human security review are not recorded.

## Evidence In Place

### Build And Test

- ✅ **CMake/CTest suite** on Windows MSVC/vcpkg preset (`ctest --preset windows-msvc-vcpkg --timeout 10`)
- ✅ **Install + package-consumer** build path
- ✅ **Release-readiness script** (`scripts/check-release-readiness.ps1 -SkipBuild`)
- ✅ **Checklist validator** (`scripts/validate-checklist.ps1`)

### Packet Protection

- ✅ `openssl_aead_protector` implements the `packet_protector` interface.
- ✅ AES-128-GCM packet protection is implemented when `FLOWQ_ENABLE_OPENSSL_CRYPTO` is enabled.
- ✅ Header protection uses the RFC 9001 initial header-protection path.
- ✅ Unsupported suites such as ChaCha20-Poly1305 and AES-256-GCM are rejected explicitly.
- ✅ AEAD creation fails closed when the OpenSSL crypto backend is not compiled in.
- ✅ Plaintext/test protection is rejected when production protection is required.

### Transport Behavior

- ✅ QUIC varint, packet number, packet header, frame, transport parameter, and packet pipeline primitives.
- ✅ ACK/loss recovery, bytes-in-flight accounting, NewReno-style congestion baseline, and deterministic timers.
- ✅ Stream receive/send state, flow-control frame handling, reset/stop handling, and retransmission behavior.
- ✅ Connection ID routing, version negotiation, retry helper surfaces, endpoint-driver lifecycle, and diagnostics.
- ✅ PATH_CHALLENGE/PATH_RESPONSE frame codec support.
- ✅ Application-space PATH_CHALLENGE scheduling emits a same-value PATH_RESPONSE.
- ✅ PATH_CHALLENGE/PATH_RESPONSE outside Application packet space closes with protocol error.

### Interop Harness

- ✅ `interop_runner` executes configured scenarios through an injected executor.
- ✅ Available peers no longer produce synthetic skip results; executor exit code, timeout, and exceptions map to pass/fail/error.
- ✅ `scripts/run-interop.ps1` and `scripts/run-interop.sh` call the built harness binary and write per-scenario JSON results.
- ✅ Harness wiring was verified locally with `FLOWQ_INTEROP_SCENARIO=basic_handshake` and a local executable peer.
- ⚠️ Named external QUIC peers are not available in the current local environment: ngtcp2, quiche, MsQuic, picoquic, and lsquic were not found on PATH; Docker daemon was not running; WSL has no available distribution.

### Hardening

- ✅ Fuzz targets: `fuzz_packet_header`, `fuzz_frame_decode`, `fuzz_qpack`.
- ✅ ASan + UBSan workflow in `.github/workflows/robustness.yml`.
- ✅ `detail::` namespace access is gated by `FLOWQ_DETAIL`.
- ✅ Inspection methods are gated by `FLOWQ_ENABLE_INSPECTION`.
- ✅ Public value-returning methods use `[[nodiscard]]` where applicable.
- ✅ Movable core types use explicit `noexcept` move operations.
- ✅ Public pointer ownership and thread-safety contracts are documented.

## Production-Candidate Scope

FlowQ can only claim production-candidate status for the exact scope backed by local evidence and external peer interop results.

**Candidate Scope Target**

- **Operating system**: Windows with MSVC/vcpkg preset evidence
- **QUIC version**: QUIC v1 structural transport behavior
- **TLS/backend**: OpenSSL-backed packet protection where enabled
- **Cipher suite**: AES-128-GCM-SHA256
- **Roles**: Client and server surfaces
- **Scenarios**: handshake path, stream echo, loss recovery, path validation primitives

**Explicitly Outside Scope**

- Full path migration workflow
- Stateless reset
- 0-RTT deployment policy
- HTTP/3 deployment
- WebTransport deployment
- QPACK deployment guarantees
- BBR/CUBIC production tuning
- Cross-platform release evidence
- Production TLS certificate-validation policy
- External security audit

## Open Gate Items

- [x] Real interop runner replaces synthetic skip behavior in `include/flowq/quic/interop_runner.hpp`.
- [ ] Basic handshake passes against named external QUIC peer versions.
- [ ] Stream echo passes against named external QUIC peer versions.
- [ ] Loss recovery passes against named external QUIC peer versions.
- [ ] Interop results are recorded in `docs/interop/results.md`.
- [ ] TLS backend and cipher-suite versions used during validation are recorded.
- [ ] Human security review is recorded.

## Forbidden Public Claims

The following public claims require evidence and must not appear outside policy documents:

- "Production-ready" requires human security review.
- "RFC-compliant" requires external peer interop evidence against named versions.
- "Secure" requires external audit or formal evidence.
- "Interoperable" requires passing scenarios against named peer versions.
