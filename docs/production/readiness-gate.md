# FlowQ Production Readiness Gate

This document defines the exact evidence required before FlowQ can change public wording from non-production baseline to production candidate.

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
