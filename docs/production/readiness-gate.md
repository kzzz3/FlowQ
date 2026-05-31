# FlowQ Production Readiness Gate

This document records the current evidence required before FlowQ can claim production-candidate status.

## Current Status

- **Level**: Production-readiness gate
- **Date**: 2026-05-31
- **Status**: Non-production

**Evidence summary**: Windows MSVC/vcpkg build with 506 tests passing, OpenSSL 3.6.1 QUIC TLS, AES-128-GCM/AES-256-GCM/ChaCha20-Poly1305 packet protection with cipher-suite-aware header protection, secure key material zeroing, and aioquic 1.3.0 interop (handshake, stream echo, loss recovery).

**Gaps**: Linux GCC/sanitizer evidence, multi-peer interop, human security review.

## Evidence In Place

### Build And Test

- ✅ Windows MSVC/vcpkg: 506 tests passing (`ctest --preset windows-msvc-vcpkg --timeout 10`)
- ✅ Install + package-consumer build path
- ✅ Clean install prefix validation
- ✅ Release-readiness scripts (`scripts/check-release-readiness.ps1`, `scripts/check-release-readiness.sh`)
- ✅ Strict production-candidate gates (`-RequireCompleteReleaseChecklist`)
- ✅ Checklist validator (`scripts/validate-checklist.ps1`)
- ⚠️ Linux GCC (`linux-gcc-vcpkg`) and ASan/UBSan (`linux-asan-ubsan`) presets exist but not executed locally

### Packet Protection

- ✅ `openssl_aead_protector` implements `packet_protector` interface
- ✅ AES-128-GCM (16-byte key, 12-byte IV, 16-byte tag)
- ✅ AES-256-GCM (32-byte key, 12-byte IV, 16-byte tag)
- ✅ ChaCha20-Poly1305 (32-byte key, 12-byte IV, 16-byte tag)
- ✅ Cipher-suite-aware header protection:
  - AES-128-GCM → AES-128-ECB (16-byte HP key)
  - AES-256-GCM → AES-256-ECB (32-byte HP key)
  - ChaCha20-Poly1305 → ChaCha20 (32-byte HP key)
- ✅ Fail-closed when OpenSSL crypto backend disabled
- ✅ Plaintext protector isolated to test support
- ✅ Secure key material zeroing on destruction (Windows SecureZeroMemory, macOS memset_s, Linux explicit_bzero)
- ✅ All protector types erase keys: `initial_packet_protector`, `openssl_aead_protector`, `traffic_key_material`

### Transport Behavior

- ✅ QUIC v1 varint, packet number, packet header, frame, transport parameter codecs
- ✅ ACK/loss recovery, RTT estimation, PTO, bytes-in-flight accounting
- ✅ NewReno congestion control (slow start, congestion avoidance, persistent congestion)
- ✅ Stream receive/send state, flow control (stream-level and connection-level)
- ✅ Connection ID routing, NEW_CONNECTION_ID, RETIRE_CONNECTION_ID
- ✅ Stateless reset detection and generation
- ✅ PATH_CHALLENGE/PATH_RESPONSE with peer migration validation
- ✅ Anti-amplification limit (3x received bytes)
- ✅ Version negotiation, retry helper surfaces
- ✅ Endpoint driver lifecycle with connection limits

### Interop

- ✅ aioquic 1.3.0: handshake, bidirectional stream echo, loss recovery (all PASS)
- ✅ TLS backend: OpenSSL 3.6.1, cipher: TLS_AES_128_GCM_SHA256
- ✅ Client: CA verification, SNI, hostname verification
- ⚠️ Only 1 external peer validated (aioquic); ngtcp2, quiche, MsQuic not available

### Hardening

- ✅ Fuzz targets: `fuzz_packet_header`, `fuzz_frame_decode`, `fuzz_qpack`
- ✅ ASan + UBSan workflow (`.github/workflows/robustness.yml`)
- ✅ `detail::` namespace gated by `FLOWQ_DETAIL`
- ✅ Inspection methods gated by `FLOWQ_ENABLE_INSPECTION`
- ✅ `[[nodiscard]]` on value-returning public methods
- ✅ `noexcept` move operations on core types
- ✅ Thread-safety contracts documented
- ✅ Code quality gates: no TODO/FIXME, no type suppressions, no empty catch, no weak RNG

## Production-Candidate Scope

**In scope**:
- QUIC v1 transport (RFC 9000)
- TLS 1.3 handshake (RFC 9001) via OpenSSL 3.5+ QUIC TLS
- Cipher suites: AES-128-GCM-SHA256, AES-256-GCM-SHA384, TLS_CHACHA20_POLY1305_SHA256
- Client and server roles
- Windows MSVC/vcpkg platform

**Out of scope**:
- 0-RTT deployment
- HTTP/3, QPACK, WebTransport
- BBR/CUBIC congestion control
- Cross-platform release evidence
- External security audit

## Open Gate Items

- [x] Interop runner with real peer execution
- [x] aioquic 1.3.0 handshake PASS
- [x] aioquic 1.3.0 stream echo PASS
- [x] aioquic 1.3.0 loss recovery PASS
- [x] Interop results recorded in `docs/interop/results.md`
- [x] TLS backend and cipher suite versions recorded
- [x] Cipher-suite-aware header protection
- [x] Secure key material zeroing across all protectors
- [ ] Linux GCC execution evidence
- [ ] ASan/UBSan execution evidence
- [ ] Second external peer interop
- [ ] Human security review

## Forbidden Public Claims

- "Production-ready" → requires human security review
- "RFC-compliant" → requires multi-peer interop evidence
- "Secure" → requires external audit
- "Interoperable" → requires 2+ named peer versions
