# FlowQ Production Readiness Gate

This document records the current evidence required before FlowQ can claim production-candidate status.

## Current Status

- **Level**: Production-readiness gate
- **Date**: 2026-05-31
- **Status**: Non-production. The codebase has local build/test evidence, OpenSSL-gated AES-128-GCM packet protection, deterministic transport behavior, and recorded aioquic handshake, bidirectional STREAM echo, and application loss-recovery passes. Multi-peer interop and human security review are not recorded.

## Evidence In Place

### Build And Test

- ✅ **CMake/CTest suite** on Windows MSVC/vcpkg preset (`ctest --preset windows-msvc-vcpkg --timeout 10`)
- ✅ **Install + package-consumer** build path
- ✅ **Clean install prefix** validation before package-consumer checks, preventing removed public headers from surviving as stale installed artifacts.
- ✅ **Release-readiness script** (`scripts/check-release-readiness.ps1 -SkipBuild`)
- ✅ **Strict production-candidate gate** (`scripts/check-release-readiness.ps1 -RequireCompleteReleaseChecklist`) fails until every required release checklist item is checked.
- ✅ **Checklist validator** (`scripts/validate-checklist.ps1`)
- ⚠️ **Linux GCC preset** (`linux-gcc-vcpkg`) and **ASan/UBSan preset** (`linux-asan-ubsan`) exist, but Linux execution evidence is not recorded in this local environment.

### Packet Protection

- ✅ `openssl_aead_protector` implements the `packet_protector` interface.
- ✅ AES-128-GCM packet protection is implemented when `FLOWQ_ENABLE_OPENSSL_CRYPTO` is enabled.
- ✅ Header protection uses the RFC 9001 initial header-protection path.
- ✅ Unsupported suites such as ChaCha20-Poly1305 and AES-256-GCM are rejected explicitly.
- ✅ AEAD creation fails closed when the OpenSSL crypto backend is not compiled in.
- ✅ Plaintext packet protection is isolated to test support and is not part of installed public headers.
- ✅ Test-only protectors are rejected when production protection is required.

### Transport Behavior

- ✅ QUIC varint, packet number, packet header, frame, transport parameter, and packet pipeline primitives.
- ✅ ACK/loss recovery, bytes-in-flight accounting, NewReno-style congestion baseline, and deterministic timers.
- ✅ Stream receive/send state, flow-control frame handling, reset/stop handling, and retransmission behavior.
- ✅ Connection ID routing, version negotiation, retry helper surfaces, endpoint-driver lifecycle, and diagnostics.
- ✅ PATH_CHALLENGE/PATH_RESPONSE frame codec support.
- ✅ Application-space PATH_CHALLENGE scheduling emits a same-value PATH_RESPONSE.
- ✅ PATH_CHALLENGE/PATH_RESPONSE outside Application packet space closes with protocol error.

### Interop Harness

- ✅ Test-support `interop_runner` executes configured scenarios through an injected executor without being installed as public API.
- ✅ Available peers no longer produce synthetic skip results; executor exit code, timeout, and exceptions map to pass/fail/error.
- ✅ `scripts/run-interop.ps1` and `scripts/run-interop.sh` call the built harness binary and write per-scenario JSON results.
- ✅ External interop wrapper scripts fail when any selected scenario is skipped, so missing peers or TLS setup cannot satisfy the gate.
- ✅ Harness wiring was verified locally with `FLOWQ_INTEROP_SCENARIO=basic_handshake` and a local executable peer.
- ✅ FlowQ client handshake and bidirectional stream 0 echo pass against Python `aioquic` 1.3.0 in conda environment `expr`.
- ✅ The aioquic validation run recorded FlowQ TLS backend OpenSSL QUIC TLS (`OpenSSL 3.6.1 27 Jan 2026`) and negotiated cipher suite `TLS_AES_128_GCM_SHA256`.
- ✅ FlowQ client application loss recovery passes against Python `aioquic` 1.3.0 after one intentionally dropped short-header datagram; FlowQ reports Application PTO loss detection and stream retransmission before receiving the echo.
- ✅ Coalesced long-header datagrams now stay in core connection processing: peer source CID learning, trailing zero padding, and packet-protector refresh are covered by unit tests and the aioquic handshake regression.
- ⚠️ Named non-aioquic QUIC peers are not available in the current local environment: ngtcp2, quiche, MsQuic, picoquic, and lsquic were not found on PATH.

### Hardening

- ✅ Fuzz targets: `fuzz_packet_header`, `fuzz_frame_decode`, `fuzz_qpack`.
- ✅ ASan + UBSan workflow in `.github/workflows/robustness.yml` uses the same vcpkg manifest dependency path as the standard Linux build.
- ✅ `detail::` namespace access is gated by `FLOWQ_DETAIL`.
- ✅ Inspection methods are gated by `FLOWQ_ENABLE_INSPECTION`.
- ✅ Public value-returning methods use `[[nodiscard]]` where applicable.
- ✅ Movable core types use explicit `noexcept` move operations.
- ✅ Public pointer ownership and thread-safety contracts are documented.
- ✅ Plaintext packet-protection helpers are kept out of the installed public API surface.
- ✅ Source-only HTTP/3, QPACK, 0-RTT, and test-support interop headers are kept out of the installed production package, with install validation rejecting regressions.

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

- [x] Real interop runner replaces synthetic skip behavior in `tests/support/interop_runner.hpp`.
- [x] Basic handshake passes against aioquic 1.3.0.
- [x] Client STREAM delivery passes against aioquic 1.3.0.
- [x] Bidirectional stream echo passes against aioquic 1.3.0.
- [x] Loss recovery passes against aioquic 1.3.0.
- [x] Interop results are recorded in `docs/interop/results.md`.
- [x] TLS backend and cipher-suite versions used during validation are recorded.
- [ ] Linux GCC preset execution evidence is recorded.
- [ ] ASan/UBSan execution evidence is recorded.
- [x] Strict-warning evidence is refreshed against the current test suite.
- [ ] Human security review is recorded.

## Forbidden Public Claims

The following public claims require evidence and must not appear outside policy documents:

- "Production-ready" requires human security review.
- "RFC-compliant" requires external peer interop evidence against named versions.
- "Secure" requires external audit or formal evidence.
- "Interoperable" requires passing scenarios against named peer versions.
