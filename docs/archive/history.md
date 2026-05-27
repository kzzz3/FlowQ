# FlowQ Development History

This document consolidates the historical development records of FlowQ.

## Phase 1-6: Core Library Development (M1-M26)

### Completed Milestones

| Phase | Milestones | Description |
|-------|------------|-------------|
| 1: Core Primitives | M1-M10 | Varint, frame, header codecs, stream state, flow control |
| 2: Connection Integration | M11-M15 | Connection-owned streams, aggregate flow control, recovery timers |
| 3: Loopback | M16-M18 | Application packet space, close/reset codecs, in-memory session |
| 4: Security Boundary | M19 | Crypto adapter seam, test-only protection boundary |
| 5: Library Surface | M20-M23 | Public session façade, UDP adapter, recovery scheduler |
| 6: Productization | M24-M26 | Examples, install/export, CI |

### Key Design Decisions

- **Values First**: Pure value types before stateful components
- **Deterministic Tests**: No network, no TLS, no random in unit tests
- **Boundary Seams**: Virtual interfaces for external dependencies
- **Fail-Closed**: Reject unsafe operations by default

## Phase 7: Production Readiness (M27-M39)

### M27: Packet Number Helpers
RFC 9000 packet-number truncation and reconstruction helpers. Required before real short-header decoding.

### M28: Crypto Provider Boundary
External provider capability evidence. FlowQ calls external HKDF/AEAD/header-protection through interface, does not implement primitives.

### M29: RFC 9001 Initial Vectors
Selected RFC 9001 packet-protection vectors through vetted OpenSSL backend. Narrow evidence claim only.

### M30: Transport Parameters
Structural transport parameter encode/decode and config mapping. TLS extension binding remains separate.

### M31: TLS Handshake Adapter
Opaque CRYPTO byte routing and handshake/key state observation. External provider owns TLS 1.3 transcript.

### M31b: OpenSSL QUIC TLS Provider
- **M31b-a**: Default-off OpenSSL 3.5+ QUIC TLS API detection surface
- **M31b-b**: Provider-backed local TLS handshake evidence

### M32: Short-Header Shell
RFC-shaped short-header value model and test-mode parser shell. Header protection removal remains future work.

### M33: Key Lifecycle
Deterministic key availability and packet-space discard gates. Does not store/export real TLS secrets.

### M34: Congestion Baseline
Bytes-in-flight accounting and NewReno-style congestion behavior. Pacing and ECN remain separate.

### M35: Connection Routing
Routing table, version negotiation, and retry interface helpers. Production server listeners remain future work.

### M36: Endpoint Driver
Production-shaped endpoint API with explicit lifecycle and connection limits.

### M37: Diagnostics
qlog-style event sink, fuzz targets, sanitizer CI workflow.

### M38: Interop Harness
Opt-in harness for testing against mature QUIC implementations.

### M39: Production Evidence Gate
Documentation checklist and status wording review. Evidence-bound claims only.

## Extensions

### 0-RTT Support
Early data support with replay protection. Integrated into connection lifecycle.

### HTTP/3
Basic frame encoding (DATA, HEADERS, SETTINGS, GOAWAY). QPACK and request/response semantics remain future work.

## Architecture Evolution

```
M1-M10:    Value types → Codecs → Protocol state machines
M11-M20:   Connection integration → Stream management → Loopback proof
M21-M27:   Public API → Library surface → Packet helpers
M28-M33:   Crypto boundaries → TLS adapter → Key lifecycle
M34-M39:   Recovery → Routing → Diagnostics → Interop → Evidence gate
```

## Lessons Learned

1. **TDD Works**: Red-Green-Refactor caught issues early
2. **Boundaries Matter**: Virtual seams enabled parallel development
3. **Evidence Over Claims**: Documentation must be backed by tests
4. **Incremental Progress**: Small milestones are easier to review

## References

- [Production Readiness Design](2026-04-27-post-basic-production-readiness-design.md)
- [Basic Complete Declaration](basic-complete.md)
- [Technical Proposal](technical-proposal.md)
