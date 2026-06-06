# FlowQ Production Readiness Gate

This document records the current evidence required before FlowQ can claim production-candidate or production-ready status.

## Current Status

- **Level**: Production-readiness gate
- **Date**: 2026-06-06
- **Status**: Release-candidate evidence assembled; production-ready wording remains blocked

**Evidence summary**: Windows MSVC/vcpkg build with 514 tests passing, Linux GCC/vcpkg build with 510/511 tests passing, ASan/UBSan verification with 0 errors, OpenSSL 3.6.1 QUIC TLS, AES-128-GCM/AES-256-GCM/ChaCha20-Poly1305 packet protection with cipher-suite-aware header protection, key material zeroing, AEAD key rotation, pacing controller, BBR/CUBIC congestion control, aioquic 1.3.0 full-flow interop (handshake, stream echo, loss recovery), and ngtcp2 1.20.0 Initial packet generation smoke evidence.

**Open gaps**: second external peer full handshake/stream evidence, human security review, external security audit.

## Evidence In Place

### Build And Test

- Windows MSVC/vcpkg: 514/516 tests passing (`ctest --preset windows-msvc-vcpkg --timeout 60`)
- Linux GCC/vcpkg: 510/511 tests passing (`ctest --preset linux-gcc-vcpkg --timeout 60`)
- ASan/UBSan: 510/511 tests passing, 0 errors (`ctest --preset linux-asan-ubsan --timeout 60`)
- Install + package-consumer build path
- Clean install prefix validation
- Release-readiness scripts (`scripts/check-release-readiness.ps1`, `scripts/check-release-readiness.sh`)
- Strict production-candidate gate tooling (`-RequireCompleteReleaseChecklist`), currently blocked by open checklist items
- Checklist validator (`scripts/validate-checklist.ps1`)

### Packet Protection

- `openssl_aead_protector` implements the `packet_protector` interface
- AES-128-GCM (16-byte key, 12-byte IV, 16-byte tag)
- AES-256-GCM (32-byte key, 12-byte IV, 16-byte tag)
- ChaCha20-Poly1305 (32-byte key, 12-byte IV, 16-byte tag)
- Cipher-suite-aware header protection:
  - AES-128-GCM: AES-128-ECB (16-byte HP key)
  - AES-256-GCM: AES-256-ECB (32-byte HP key)
  - ChaCha20-Poly1305: ChaCha20 (32-byte HP key)
- Fail-closed when OpenSSL crypto backend is disabled
- Plaintext protector isolated to test support
- Key material zeroing on destruction across `initial_packet_protector`, `openssl_aead_protector`, and `traffic_key_material`
- AEAD key rotation (RFC 9000 Section 6) with key_update_state and key_update_manager
- `traffic_secret()` restricted to `FLOWQ_ENABLE_INSPECTION`

### Transport Behavior

- QUIC v1 varint, packet number, packet header, frame, transport parameter codecs
- ACK/loss recovery, RTT estimation, PTO, bytes-in-flight accounting
- NewReno congestion control
- BBR congestion control
- CUBIC congestion control (RFC 8312, TCP friendliness, fast convergence)
- Pacing controller (RFC 9002 Section 7.7)
- Stream receive/send state and stream/connection flow control
- Connection ID routing, NEW_CONNECTION_ID, RETIRE_CONNECTION_ID
- Stateless reset detection and generation
- PATH_CHALLENGE/PATH_RESPONSE with peer migration validation
- Anti-amplification limit (3x received bytes)
- Version negotiation and retry helper surfaces
- Endpoint driver lifecycle with connection limits

### Interop

- aioquic 1.3.0: handshake, bidirectional stream echo, loss recovery
- ngtcp2 1.20.0: Initial packet generation smoke
- TLS backend: OpenSSL 3.6.1, cipher: TLS_AES_128_GCM_SHA256
- Client verification path: CA verification, SNI, hostname verification
- Full handshake/stream external peers: 1 (`aioquic`)
- `quiche`, `MsQuic`, and `picoquic` full-flow evidence is not recorded

### Hardening

- Fuzz targets: `fuzz_packet_header`, `fuzz_frame_decode`, `fuzz_qpack`
- ASan + UBSan workflow (`.github/workflows/robustness.yml`)
- `detail::` namespace gated by `FLOWQ_DETAIL`
- Inspection methods gated by `FLOWQ_ENABLE_INSPECTION`
- `[[nodiscard]]` on value-returning public methods
- `noexcept` move operations on core types
- Thread-safety contracts documented
- Code quality gates: no TODO/FIXME, no type suppressions, no empty catch, no weak RNG

## Production-Candidate Scope

In scope:

- QUIC v1 transport (RFC 9000)
- TLS 1.3 handshake (RFC 9001) via OpenSSL 3.5+ QUIC TLS
- Cipher suites: AES-128-GCM-SHA256, AES-256-GCM-SHA384, TLS_CHACHA20_POLY1305_SHA256
- Client and server roles
- Windows MSVC/vcpkg and Linux GCC/vcpkg release-gate evidence

Out of scope:

- 0-RTT deployment
- HTTP/3, QPACK, WebTransport
- Second full-flow external peer claim until recorded
- External security audit
- Public production-ready or secure claims

## Open Gate Items

- [x] Interop runner with real peer execution
- [x] aioquic 1.3.0 handshake PASS
- [x] aioquic 1.3.0 stream echo PASS
- [x] aioquic 1.3.0 loss recovery PASS
- [x] ngtcp2 1.20.0 Initial packet generation smoke PASS
- [x] Interop results recorded in `docs/interop/results.md`
- [x] TLS backend and cipher suite versions recorded
- [x] Cipher-suite-aware header protection
- [x] Key material zeroing across all protectors
- [x] Linux GCC execution evidence (510/511 tests passing)
- [x] ASan/UBSan execution evidence (0 errors)
- [ ] Second external peer full handshake and stream scenario PASS
- [ ] Human security review recorded
- [ ] External security audit recorded before public secure or production-ready claims

## Forbidden Public Claims

- "Production-ready" requires a completed release checklist and human security review.
- "RFC-compliant" requires current RFC traceability and multi-peer full-flow interop evidence.
- "Secure" requires an external audit.
- "Interoperable" requires 2+ named peer versions with full handshake and stream scenarios.
