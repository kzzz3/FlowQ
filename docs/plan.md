# FlowQ Plan

## Project Goal

FlowQ is a C++20 QUIC transport library moving toward a narrow production-candidate scope backed by local verification, OpenSSL-gated packet protection, and external peer interop evidence.

## Core Feature Breakdown

- [x] Establish CMake/vcpkg project structure with Catch2 tests.
- [x] Add ASIO sender boundaries for timers and UDP operations.
- [x] Add deterministic protocol-core action seams.
- [x] Add QUIC varint, frame, and long-header structural codecs.
- [x] Add ACK/loss and recovery primitives.
- [x] Add long-packet assembly/parsing through a packet protection seam.
- [x] Add minimal Initial/Handshake connection loop behavior.
- [x] Add stream receive, stream send, and flow-control core behavior.
- [x] Add connection stream integration, aggregate flow control, packet budgeting, recovery timers, and stream ACK/loss mapping.
- [x] M16: Add a clearly non-production structural Application packet-number space for tests and loopback.
- [x] M17: Add minimal close/reset structural codecs and stream effects.
- [x] M18: Prove a basic in-memory QUIC transport loopback session.
- [x] M19: Document and enforce the crypto adapter seam for TLS and packet-protection integration.
- [x] M20: Freeze basic-complete QUIC library scope and public API contract.
- [x] M21: Add a public QUIC session façade over the deterministic connection loop.
- [x] M22: Add a bounded UDP/ASIO session adapter.
- [x] M23: Add ASIO recovery timer scheduling integration.
- [x] M24: Add library examples and public smoke tests.
- [x] M25: Add CMake install/export packaging and package-consumer test.
- [x] M26: Add CI and release-gate documentation.
- [x] M27: Add RFC 9000 packet-number truncation and reconstruction helpers.
- [x] M28: Add crypto provider boundary and fail-closed packet protection contract.
- [x] M29: Pass selected RFC 9001 Initial packet-protection vectors through vetted primitives.
- [x] M30: Add structural transport parameter codec and config mapping.
- [x] M31: Add TLS handshake adapter boundary and CRYPTO byte pump.
- [x] M31b-a: Add default-off OpenSSL QUIC TLS provider surface.
- [x] M31b-b: Add provider-backed local TLS handshake evidence.
- [x] M32: Add RFC-shaped short-header value model and parser shell.
- [x] M33: Add key lifecycle gates and packet-space discard rules.
- [x] M34: Add recovery and congestion-control production baseline.
- [x] M35: Add connection ID routing, version negotiation, Retry, and address-validation preparation.
- [x] M36: Add production-shaped UDP endpoint lifecycle and public API hardening.
- [x] M37: Add diagnostics, qlog-style events, fuzzing, and sanitizer gates.
- [x] M38: Add opt-in interop harness against mature QUIC implementations.
- [x] M39: Add production release evidence gate and status wording review.

## Tech Stack and Architecture Decisions

- **C++20**: matches the library goal of modern systems-level networking with strong value types and deterministic tests.
- **CMake 3.25+**: standard C++ build orchestration with preset-based workflows.
- **vcpkg manifest mode**: reproducible dependency resolution for Asio, stdexec, and Catch2.
- **standalone Asio**: mature asynchronous I/O foundation without requiring Boost.
- **stdexec**: sender/receiver direction isolated behind small seams.
- **Catch2**: readable, focused unit tests for protocol values and connection behavior.

Architecture follows value codecs first, deterministic tests second, connection integration third, and endpoint/interop evidence last. The current production-candidate boundary is evidence-based: AES-128-GCM packet protection exists behind OpenSSL, plaintext/test protection is rejected by production policy, and external peer interop evidence remains the main open gate.

## Project Structure

- `include/flowq/`: public header-only library surface and protocol modules.
- `include/flowq/quic/`: QUIC transport value types, codecs, stream state, packet pipeline, ACK/loss, recovery, and connection loop.
- `tests/unit/`: deterministic Catch2 unit tests, grouped by protocol module.
- `tests/integration/`: deterministic in-memory loopback tests.
- `tests/interop/`: interop test scenarios and runner.
- `tests/fuzz/`: fuzz targets for protocol parsers.
- `examples/`: example applications organized by feature.
- `docs/`: documentation organized by category (guides, milestones, production, reference).
- `scripts/`: utility scripts for validation and documentation generation.
- `build/`: generated local build output; not source of truth.

## Development Phases and Milestones

### Phase 1: MVCV - Minimum Viable & Clear Core

Completed through M10: deterministic protocol primitives, packet pipeline, minimal connection loop, and stream send/receive state.

### Phase 2: Connection Integration Baseline

Completed through M15: connection-owned streams, connection flow control, payload budgeting, recovery timer values, and packet-to-stream ACK/loss mapping.

### Phase 3: Basic Usable Non-Production Loopback

- [x] M16 Application Data structural packet space.
- [x] M17 minimal close/reset codec behavior.
- [x] M18 in-memory QUIC transport loopback session.

### Phase 4: Security Boundary Documentation

- [x] M19 crypto adapter seam and explicit unsafe test-protection boundary.

### Phase 5: Basic QUIC Library Surface

- [x] M20 basic-complete scope freeze.
- [x] M21 public session façade.
- [x] M22 UDP/ASIO session adapter.
- [x] M23 recovery timer scheduling adapter.

### Phase 6: Library Productization

- [x] M24 examples and smoke tests.
- [x] M25 install/export packaging.
- [x] M26 CI and basic-complete release documentation.

### Phase 7: Post-Basic Production QUIC Tracks

- [x] External TLS/crypto adapter implementation track.
- [x] M27 packet-number truncation/reconstruction helper foundation.
- [x] M31b-b local TLS handshake evidence and follow-up key export foundations.
- [x] M34-M36 congestion-control, connection routing, and production-shaped endpoint foundations.
- [x] M37-M39 diagnostics, fuzzing, interop harness, and production release evidence gates.
- [x] HTTP/3 and WebTransport backlog tracks after transport production-readiness evidence.

## Risk Assessment

- **Application packet modeling risk**: Application packet counters and trackers must remain independent from Initial and Handshake state.
- **Reset/stop scope risk**: reset/stop behavior is covered structurally; application-level cancellation semantics require explicit API evidence before being claimed.
- **Loopback scope risk**: deterministic in-memory and local UDP smoke paths are not external peer interop evidence.
- **Security claim risk**: plaintext/test protection must remain outside production-required packet-protection policy.
- **Scope creep risk**: documented production-candidate scope must match implemented AES-128-GCM, header protection, recovery, routing, endpoint, and path-validation evidence.
- **Library surface risk**: the public session façade should avoid exposing raw internal frame queues as the primary consumer API.
- **Packaging risk**: examples and CMake package consumption must build from documented commands, not only from the monorepo test binary.
- **Tooling risk**: local LSP diagnostics currently cannot run because `clangd` is not installed in this environment; MSVC build and CTest are the executable verification gates.
- **Documentation drift risk**: `docs/plan.md`, `README.md`, and milestone docs must be updated together when implementation scope evolves.
- **Production wording risk**: public status wording must remain evidence-bound; no local-only milestone is enough to claim production readiness, security, or interoperability.
- **Crypto provider boundary risk**: provider capability evidence must not be treated as an audit result or TLS certificate-policy proof.
- **Crypto vector risk**: selected RFC 9001 vectors prove packet-protection primitives, not complete peer interoperability.
- **Transport-parameter risk**: encoding, decoding, unknown preservation, and config mapping exist; authenticated negotiation must be proven through TLS/interop paths.
- **TLS handshake boundary risk**: CRYPTO byte routing and state observation exist; certificate policy and transcript correctness need explicit evidence.
- **TLS provider surface risk**: OpenSSL QUIC TLS API availability is modeled; interop runs must record backend versions and negotiated parameters.
- **Short-header risk**: short-header modeling and packet-protection seams must stay aligned with 1-RTT AEAD and packet-number reconstruction tests.
- **Key lifecycle risk**: deterministic availability and packet-space discard gates exist; live AEAD key update installation is still outside current evidence.
- **Congestion baseline risk**: deterministic bytes-in-flight accounting and NewReno-style congestion behavior exist; pacing, ECN, and production performance tuning remain separate.
- **Connection routing risk**: routing table, version negotiation, retry helpers, and endpoint lifecycle exist; full address validation and peer interop remain open gates.
