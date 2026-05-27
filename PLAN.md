# FlowQ Plan

## Project Goal

FlowQ is a modern C++ protocol library that builds a deterministic, testable, non-production QUIC-like core before exposing real network and security integrations.

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
- [x] M18: Prove a basic in-memory QUIC-like loopback session.
- [x] M19: Document and enforce the crypto adapter seam for future real TLS/packet protection.
- [x] M20: Freeze basic-complete QUIC library scope and public API contract.
- [x] M21: Add a public QUIC session façade over the deterministic connection loop.
- [x] M22: Add a non-production UDP/ASIO session adapter.
- [x] M23: Add ASIO recovery timer scheduling integration.
- [x] M24: Add library examples and public smoke tests.
- [x] M25: Add CMake install/export packaging and package-consumer test.
- [x] M26: Add CI and declare the basic complete non-production library baseline.
- [x] M27: Add RFC 9000 packet-number truncation and reconstruction helpers.
- [x] M28: Add crypto provider boundary and fail-closed packet protection contract.
- [x] M29: Pass selected RFC 9001 Initial packet-protection vectors through vetted primitives.
- [x] M30: Add structural transport parameter codec and config mapping.
- [x] M31: Add TLS handshake adapter boundary and CRYPTO byte pump.
- [x] M31b-a: Add default-off OpenSSL QUIC TLS provider surface.
- [ ] M31b-b: Add provider-backed local TLS handshake evidence.
- [x] M32: Add RFC-shaped short-header value model and parser shell.
- [x] M33: Add key lifecycle gates and packet-space discard rules.
- [x] M34: Add recovery and congestion-control production baseline.
- [x] M35: Add connection ID routing, version negotiation, Retry, and address-validation preparation.
- [ ] M36: Add production-shaped UDP endpoint lifecycle and public API hardening.
- [ ] M37: Add diagnostics, qlog-style events, fuzzing, and sanitizer gates.
- [ ] M38: Add opt-in interop harness against mature QUIC implementations.
- [ ] M39: Add production release evidence gate and status wording review.

## Tech Stack and Architecture Decisions

- **C++20**: matches the library goal of modern systems-level networking with strong value types and deterministic tests.
- **CMake 3.25+**: standard C++ build orchestration with preset-based workflows.
- **vcpkg manifest mode**: reproducible dependency resolution for Asio, stdexec, and Catch2.
- **standalone Asio**: mature asynchronous I/O foundation without requiring Boost.
- **stdexec**: sender/receiver direction for future execution APIs, currently isolated behind small seams.
- **Catch2**: readable, focused unit tests for protocol values and connection behavior.

Architecture follows values first, deterministic tests second, connection integration third, and loopback proof last. Production TLS, AEAD, header protection, congestion control, and public socket APIs remain outside the current baseline until explicit adapter boundaries exist.

## Project Structure

- `include/flowq/`: public header-only library surface and protocol modules.
- `include/flowq/quic/`: QUIC-like value types, codecs, stream state, packet pipeline, ACK/loss, recovery, and connection loop.
- `tests/unit/`: deterministic Catch2 unit tests, grouped by protocol module.
- `docs/development.md`: living development guide and implemented milestone scope notes.
- `docs/superpowers/specs/`: milestone design documents.
- `docs/superpowers/plans/`: milestone execution plans with checklist progress.
- `build/`: generated local build output; not source of truth.

## Development Phases and Milestones

### Phase 1: MVCV - Minimum Viable & Clear Core

Completed through M10: deterministic protocol primitives, packet pipeline, minimal connection loop, and stream send/receive state.

### Phase 2: Connection Integration Baseline

Completed through M15: connection-owned streams, connection flow control, payload budgeting, recovery timer values, and packet-to-stream ACK/loss mapping.

### Phase 3: Basic Usable Non-Production Loopback

- [x] M16 Application Data structural packet space.
- [x] M17 minimal close/reset codec behavior.
- [x] M18 in-memory QUIC-like loopback session.

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

- [ ] External TLS/crypto adapter implementation track.
- [x] M27 packet-number truncation/reconstruction helper foundation.
- [ ] M31b-b local TLS handshake evidence and follow-up key export foundations.
- [ ] M34-M36 congestion-control, connection routing, and production-shaped endpoint foundations.
- [ ] M37-M39 diagnostics, fuzzing, interop harness, and production release evidence gates.
- [ ] HTTP/3 and WebTransport backlog tracks after transport production-readiness evidence.

## Risk Assessment

- **Application packet modeling risk**: M16 now has independent Application packet counters and trackers; future changes must preserve this and avoid aliasing Application state into Initial or Handshake.
- **Reset/stop scope risk**: M17 reset/stop behavior is structural and test-visible only; future work must avoid treating it as a complete stream lifecycle or application cancellation API.
- **Loopback scope risk**: M18 proves deterministic in-memory session behavior only; future work must not present it as socket, TLS, or interoperable QUIC support.
- **Security claim risk**: M19 adds explicit protector capability reporting and production-required rejection for test-only protection; future docs must continue to avoid describing plaintext/test protection as secure QUIC.
- **Scope creep risk**: full protected short-header parsing, TLS, AEAD, header protection, congestion control, and production/interoperable UDP public APIs remain deferred beyond this baseline; M22 covers only a bounded non-production UDP/ASIO smoke adapter.
- **Library surface risk**: M20-M23 must expose a usable QUIC library façade without leaking raw internal frame queues as the primary consumer API.
- **Packaging risk**: M24-M26 must prove examples and CMake package consumption build from documented commands, not only from the monorepo test binary.
- **Tooling risk**: local LSP diagnostics currently cannot run because `clangd` is not installed in this environment; MSVC build and CTest are the executable verification gates.
- **Documentation drift risk**: this `PLAN.md`, `README.md`, `docs/development.md`, and milestone docs must be updated together when implementation scope evolves.
- **Production wording risk**: M28-M39 must keep status evidence-bound; no single milestone is enough to claim production readiness, security, or interoperability.
- **Crypto provider boundary risk**: M28 adds external provider capability evidence only; future work must not treat provider-shaped values as an in-tree crypto backend or a production security claim.
- **Crypto vector risk**: M29 validates selected RFC 9001 Initial vectors through OpenSSL only when explicitly enabled; passing these vectors is not a production TLS, packet-protection, or interoperability claim.
- **Transport-parameter risk**: M30 encodes, decodes, preserves unknown parameters, and maps selected values into config only; TLS extension binding and authenticated negotiation remain future work.
- **TLS handshake boundary risk**: M31 routes opaque CRYPTO bytes and observes handshake/key state only; external provider wiring, certificate validation, TLS transcript handling, and real key schedule remain M31b/future work.
- **TLS provider surface risk**: M31b-a adds only default-off OpenSSL QUIC TLS API detection and provider metadata; complete TLS handshakes, certificate-policy validation, and key lifecycle proof remain future work.
- **Short-header shell risk**: M32 models RFC-shaped short headers and a test-mode parser shell only; header-protection removal, packet-number reconstruction from protected headers, 1-RTT AEAD, and interoperability remain future work.
- **Key lifecycle risk**: M33 tracks deterministic availability and packet-space discard gates only; external TLS/crypto must still supply real secrets, key material, key updates, and packet-protection installation.
+- **Congestion baseline risk**: M34 adds deterministic bytes-in-flight accounting and NewReno-style congestion behavior only; pacing, ECN, and production performance tuning remain separate.
+- **Connection routing risk**: M35 adds deterministic routing table, version negotiation, and retry interface helpers only; production server listeners, real retry integrity, and full address-validation remain future work.
