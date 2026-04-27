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

## Risk Assessment

- **Application packet modeling risk**: M16 now has independent Application packet counters and trackers; future changes must preserve this and avoid aliasing Application state into Initial or Handshake.
- **Reset/stop scope risk**: M17 reset/stop behavior is structural and test-visible only; future work must avoid treating it as a complete stream lifecycle or application cancellation API.
- **Loopback scope risk**: M18 proves deterministic in-memory session behavior only; future work must not present it as socket, TLS, or interoperable QUIC support.
- **Security claim risk**: M19 adds explicit protector capability reporting and production-required rejection for test-only protection; future docs must continue to avoid describing plaintext/test protection as secure QUIC.
- **Scope creep risk**: real short-header packet number reconstruction, TLS, AEAD, header protection, congestion control, and UDP public APIs remain deferred beyond this baseline.
- **Tooling risk**: local LSP diagnostics currently cannot run because `clangd` is not installed in this environment; MSVC build and CTest are the executable verification gates.
- **Documentation drift risk**: this `PLAN.md`, `README.md`, `docs/development.md`, and milestone docs must be updated together when implementation scope evolves.
