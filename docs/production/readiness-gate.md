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
