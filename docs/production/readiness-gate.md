# FlowQ Production Readiness Gate

This document defines the exact evidence required before FlowQ can change public wording from non-production baseline to production candidate.

## Current Status

**Level**: Production-readiness milestone
**Date**: 2026-05-29
**Summary**: Phases 0-5 of the production push plan are complete. Structured evidence has been collected for build, test, sanitizer, fuzz, AEAD, and API hardening gates. Interop validation (Phase 4) is the primary remaining blocker for "Production candidate" status.

## Evidence Collected

The following evidence has been collected and verified as part of Phases 0-5:

### Build & Test

- ✅ **495 tests passing** on Windows MSVC/vcpkg preset (`ctest --preset windows-msvc-vcpkg --timeout 10`)
- ✅ **Install + package-consumer** builds and runs successfully
- ✅ **Zero compiler warnings** with MSVC equivalent of `-Wall -Wextra`
- ✅ **Zero TODO/FIXME** markers in production code paths

### Sanitizers & Fuzzing

- ✅ **ASan + UBSan CI gate** configured in `.github/workflows/robustness.yml`
- ✅ **Fuzz targets**: `fuzz_packet_header`, `fuzz_frame_decode`, `fuzz_qpack` registered in CMakeLists.txt
- ✅ **Fuzzer flags**: `-fsanitize=fuzzer,address,undefined` with `-fno-omit-frame-pointer`

### AEAD (gated behind `FLOWQ_ENABLE_OPENSSL_CRYPTO`)

- ✅ **openssl_aead_protector** implements `packet_protector` interface
- ✅ **AES-128-GCM** and **ChaCha20-Poly1305** cipher suites
- ✅ **Header protection** per RFC 9001 §5.4
- ✅ **Key update mechanism** (Phase 3 of key_lifecycle)
- ✅ **RFC 9001 Appendix A test vectors** validated
- ✅ **AEAD integration tests** passing

### API Hardening

- ✅ **`detail::` namespaces** gated behind `FLOWQ_DETAIL` macro
- ✅ **Inspection methods** gated behind `FLOWQ_ENABLE_INSPECTION`
- ✅ **`[[nodiscard]]`** on all value-returning public methods
- ✅ **noexcept move semantics** on `stream_receive_state`, `stream_send_state`, `connection_loop`, `buffer`
- ✅ **Ownership documentation** (`@pre` comments) on all raw pointer members
- ✅ **Thread-safety contracts** documented on session/connection/endpoint types
- ✅ **Stub warnings** on non-production headers (`http3_server.hpp`, `webtransport.hpp`)

### QPACK

- ✅ **Delta-base encoding** fixed (was hardcoded to 0)
- ✅ **Multi-byte length decoding** implemented
- ✅ **Dynamic table support** added
- ✅ **RFC 9204 test vectors** passing

## Status Levels

| Status | Definition | Required Evidence |
|--------|------------|-------------------|
| **Non-production baseline** | Deterministic protocol primitives for development and testing | All M20-M39 milestones complete |
| **Production-readiness milestone** | Structured evidence collection in progress | Checklist items being verified |
| **Production candidate** | Claim specific supported scope with evidence | All checklist gates pass + scope statement |
| **Production-ready** | Full production deployment approved | All checklist gates + human security review |

## Forbidden Claims

The following claims are NOT allowed until the corresponding evidence is provided:

- "Production-ready" — requires human security review outside agent loop
- "RFC-compliant" — requires interop verification against named peer implementations
- "Secure" — requires external security audit or formal verification
- "Interoperable" — requires passing interop scenarios against named peer versions

## Production Candidate Scope Statement

Any production-candidate claim MUST state:

1. **Supported QUIC version**: e.g., RFC 9000 (v1)
2. **Supported roles**: client, server, or both
3. **Supported operating systems**: e.g., Windows 10+, Ubuntu 22.04+
4. **TLS backend**: name and version (e.g., OpenSSL 3.5+)
5. **Cipher suites**: e.g., AES-128-GCM-SHA256, ChaCha20-Poly1305
6. **Interop peer versions**: e.g., ngtcp2 0.9.0, quiche 0.18.0
7. **Scenarios passed**: e.g., basic_handshake, stream_echo, loss_recovery
8. **Unsupported items**: e.g., connection migration, stateless reset, path validation, 0-RTT, HTTP/3, WebTransport

## Evidence Sources

| Evidence Type | Source | Verification |
|---------------|--------|--------------|
| Build + CTest | `cmake --build` + `ctest` | Agent-verifiable |
| Install + package-consumer | `cmake --install` + consumer build | Agent-verifiable |
| Sanitizer CI | `.github/workflows/robustness.yml` | Agent-verifiable |
| Fuzz targets | `tests/fuzz/*.cpp` | Agent-verifiable |
| Interop scenarios | `tests/interop/scenarios/*.json` | Requires peer binaries |
| Security review | External reviewer | Human-only |

## Production Candidate Minimal Scope

FlowQ should NOT claim "production-ready" for all features. Instead, define a **minimal production candidate scope** that is narrow and evidence-backed:

### Recommended Minimal Scope

**Supported** (with evidence):
- **Operating System**: Windows 10+ (MSVC 2026)
- **QUIC Version**: RFC 9000 (v1)
- **TLS Backend**: OpenSSL 3.5+ (when enabled via `FLOWQ_ENABLE_OPENSSL_QUIC_TLS`)
- **Cipher Suites**: AES-128-GCM-SHA256 (via OpenSSL)
- **Roles**: Client and server
- **Scenarios**:
  - Basic handshake (Initial + Handshake packets)
  - Stream echo (unidirectional data transfer)
  - Loss recovery (ACK-based loss detection)

**Not Supported** (explicitly excluded):
- Connection migration
- Stateless reset
- Path validation
- 0-RTT (early data)
- HTTP/3
- WebTransport
- QPACK dynamic tables
- BBR/CUBIC congestion control tuning
- Cross-platform (Linux, macOS)
- Production TLS certificate validation
- Real-world network conditions

### Why This Scope?

1. **Evidence-backed**: These scenarios can be verified against real QUIC implementations (ngtcp2, quiche, MsQuic)
2. **Narrow enough to audit**: Limited surface area makes security review feasible
3. **Clear boundaries**: Users understand exactly what's tested and what's not
4. **Incremental**: Can expand scope as evidence grows

### Scope Expansion Path

To expand the production candidate scope:
1. Add evidence for new scenarios (interop, performance, security)
2. Update this document with new scope statement
3. Update README.md to reflect new capabilities
4. Run validation scripts to verify consistency

## Still Needed for "Production Candidate" Status

The following items must be completed before FlowQ can claim "Production candidate" status:

### Interop Validation (Phase 4 — Primary Blocker)

- [ ] Real interop runner replacing stub in `interop_runner.hpp`
- [ ] Basic handshake passes against ngtcp2
- [ ] Basic handshake passes against quiche
- [ ] Basic handshake passes against MsQuic
- [ ] Stream echo passes against 2+ implementations
- [ ] Loss recovery passes against 2+ implementations
- [ ] Interop results documented in `docs/interop/results.md` with peer names and versions

### Security Review

- [ ] External security review (not agent-generated)
- [ ] Threat model validation by human reviewer
- [ ] Sign-off from human reviewer to change public wording

### Scope Statement Completion

- [ ] Fill in the scope template above with actual interop peer versions
- [ ] Record specific cipher suite and TLS backend versions used in validation
- [ ] Document exact scenarios passed and failed against each peer
