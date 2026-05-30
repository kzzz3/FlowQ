# FlowQ Milestone Roadmap

Complete milestone tracking for FlowQ development.

## Status Legend

- ✅ Complete - Implemented, tested, and documented
- 🔄 In Progress - Actively being developed
- ⏳ Planned - Defined but not started

## Milestone Index

### Core Library (M1-M26) ✅

| Milestone | Status | Description |
|-----------|--------|-------------|
| M1 | ✅ | QUIC varint codec foundation |
| M2 | ✅ | Transport frame value model and codecs |
| M3 | ✅ | Long-header packet value model and codecs |
| M4 | ✅ | Packet pipeline assembly and parsing |
| M5 | ✅ | ACK frame tracking primitives |
| M6 | ✅ | Stream send and receive state model |
| M7 | ✅ | Stream frame delivery into stream state |
| M8 | ✅ | Connection-level frame dispatch |
| M9 | ✅ | Flow-control accounting |
| M10 | ✅ | Deterministic connection loop smoke behavior |
| M11 | ✅ | Connection-owned stream registry |
| M12 | ✅ | Aggregate connection flow control |
| M13 | ✅ | Payload budgeting across stream frames |
| M14 | ✅ | Recovery timer value computation |
| M15 | ✅ | Packet-to-stream ACK/loss mapping |
| M16 | ✅ | Application packet-space structural support |
| M17 | ✅ | Close/reset structural codecs and effects |
| M18 | ✅ | In-memory transport loopback session |
| M19 | ✅ | Security boundary: crypto adapter seam, test-only protection boundary |
| M20 | ✅ | Basic-complete library scope and public API contract |
| M21 | ✅ | Public QUIC session façade over deterministic connection loop |
| M22 | ✅ | Bounded UDP/ASIO session adapter |
| M23 | ✅ | ASIO recovery timer scheduling integration |
| M24 | ✅ | Library examples and public smoke tests |
| M25 | ✅ | CMake install/export package and package-consumer test |
| M26 | ✅ | CI and release-gate documentation |

### Production Readiness (M27-M39) ✅

| Milestone | Status | Description |
|-----------|--------|-------------|
| M27 | ✅ | RFC 9000 packet-number truncation and reconstruction helpers |
| M28 | ✅ | Crypto provider boundary and fail-closed packet protection contract |
| M29 | ✅ | RFC 9001 Initial packet-protection vectors through vetted primitives |
| M30 | ✅ | Structural transport parameter codec and config mapping |
| M31 | ✅ | TLS handshake adapter boundary and CRYPTO byte pump |
| M31b-a | ✅ | Default-off OpenSSL QUIC TLS provider surface |
| M31b-b | ✅ | Provider-backed local TLS handshake evidence |
| M32 | ✅ | RFC-shaped short-header value model and parser shell |
| M33 | ✅ | Key lifecycle gates and packet-space discard rules |
| M34 | ✅ | Recovery and congestion-control production baseline |
| M35 | ✅ | Connection ID routing, version negotiation, Retry preparation |
| M36 | ✅ | Production-shaped UDP endpoint lifecycle and public API hardening |
| M37 | ✅ | Diagnostics, qlog-style events, fuzzing, and sanitizer gates |
| M38 | ✅ | Opt-in interop harness against mature QUIC implementations |
| M39 | ✅ | Production release evidence gate and status wording review |

### Extensions ✅

| Feature | Status | Description |
|---------|--------|-------------|
| 0-RTT | ✅ | Early data support with replay protection |
| HTTP/3 | ✅ | Basic frame encoding (DATA, HEADERS, SETTINGS, GOAWAY) |

## Detailed Milestone Plans

### M28: Crypto Provider Boundary

**Goal**: Define a provider boundary for vetted crypto libraries.

**Files**: `crypto_provider.hpp`, `quic_crypto_provider_tests.cpp`

**TDD Steps**:
- ✅ Write RED tests proving backend absence produces disabled provider result
- ✅ Write RED tests proving provider metadata exposes capabilities
- ✅ Implement provider boundary and fail-closed contract

### M29: RFC 9001 Initial Vectors

**Goal**: Pass selected RFC 9001 packet-protection vectors through vetted primitives.

**Files**: `initial_keys.hpp`, `quic_initial_keys_tests.cpp`

**TDD Steps**:
- ✅ Write RED tests for HKDF extract/expand
- ✅ Write RED tests for AES-128-GCM seal/open
- ✅ Implement OpenSSL-backed vector verification

### M30: Transport Parameters

**Goal**: Add structural transport parameter codec and config mapping.

**Files**: `transport_parameters.hpp`, `quic_transport_parameters_tests.cpp`

**TDD Steps**:
- ✅ Write RED tests for parameter encoding/decoding
- ✅ Write RED tests for config mapping
- ✅ Implement parameter codec

### M31: TLS Handshake Adapter

**Goal**: Add opaque CRYPTO byte routing and handshake/key state observation.

**Files**: `tls_handshake.hpp`, `quic_tls_handshake_tests.cpp`

**TDD Steps**:
- ✅ Write RED tests for adapter interface
- ✅ Write RED tests for CRYPTO byte flow
- ✅ Implement adapter boundary

### M31b: OpenSSL QUIC TLS Provider

**Goal**: Add default-off OpenSSL 3.5+ QUIC TLS provider surface.

**Files**: `tls_provider_backend.hpp`, `openssl_tls_handshake.hpp`

**TDD Steps**:
- ✅ Write RED tests for backend status reporting
- ✅ Write RED tests for API availability detection
- ✅ Implement provider metadata and handshake adapter

### M32: Short-Header Shell

**Goal**: Add RFC-shaped short-header value model and parser shell.

**Files**: `packet_header.hpp`, `packet_pipeline.hpp`

**TDD Steps**:
- ✅ Write RED tests for short-header fields
- ✅ Write RED tests for test-mode parsing
- ✅ Implement short-header model

### M33: Key Lifecycle

**Goal**: Add deterministic key availability and packet-space discard gates.

**Files**: `key_lifecycle.hpp`, `quic_key_lifecycle_tests.cpp`

**TDD Steps**:
- ✅ Write RED tests for key availability events
- ✅ Write RED tests for packet-space discard
- ✅ Implement lifecycle state machine

### M34: Congestion Baseline

**Goal**: Add bytes-in-flight accounting and NewReno-style congestion behavior.

**Files**: `congestion.hpp`, `quic_congestion_tests.cpp`

**TDD Steps**:
- ✅ Write RED tests for bytes-in-flight tracking
- ✅ Write RED tests for slow start/congestion avoidance
- ✅ Implement congestion controller

### M35: Connection Routing

**Goal**: Add routing table, version negotiation, and retry interface helpers.

**Files**: `connection_routing.hpp`, `quic_connection_routing_tests.cpp`

**TDD Steps**:
- ✅ Write RED tests for CID lookup/retirement
- ✅ Write RED tests for version negotiation
- ✅ Implement routing and retry helpers

### M36: Endpoint Driver

**Goal**: Add production-shaped endpoint API with explicit lifecycle.

**Files**: `endpoint_driver.hpp`, `quic_endpoint_driver_tests.cpp`

**TDD Steps**:
- ✅ Write RED tests for lifecycle management
- ✅ Write RED tests for connection limits
- ✅ Implement endpoint driver

### M37: Diagnostics

**Goal**: Add qlog-style event sink, fuzz targets, sanitizer CI.

**Files**: `diagnostics.hpp`, `quic_diagnostics_tests.cpp`, fuzz targets, `robustness.yml`

**TDD Steps**:
- ✅ Write RED tests for event types
- ✅ Write RED tests for event collection
- ✅ Implement diagnostic sink and fuzz targets

### M38: Interop Harness

**Goal**: Add opt-in harness for testing against mature QUIC implementations.

**Files**: `tests/interop/`, scenario files

**TDD Steps**:
- ✅ Write RED tests for scenario parsing
- ✅ Write RED tests for skip behavior
- ✅ Implement interop harness

### M39: Production Evidence Gate

**Goal**: Define evidence requirements for production claims.

**Files**: `production/readiness-gate.md`, `production/release-checklist.md`

**TDD Steps**:
- ✅ Add documentation checklist
- ✅ Add status wording guidelines
- ✅ Verify docs do not contain forbidden claims

## Execution Policy

For each milestone:

1. Write failing tests first (RED)
2. Verify RED for the expected reason
3. Implement minimal production code
4. Verify focused GREEN
5. Run full CTest, install/export, and package-consumer
6. Run `git diff --check` and changed-file scan
7. Request Oracle review before moving on

## References

- [Production Readiness Gate](../production/readiness-gate.md)
- [Release Checklist](../production/release-checklist.md)
- [Architecture](../reference/architecture.md)
