# FlowQ

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![CMake](https://img.shields.io/badge/build-CMake-informational)
![Tests](https://img.shields.io/badge/tests-Catch2-green)

FlowQ is a modern C++ QUIC-like protocol library that builds deterministic protocol primitives first, then layers connection behavior and loopback tests without claiming production QUIC security.

## Design Overview

FlowQ is intentionally staged:

1. Pure value codecs and protocol state machines.
2. Deterministic unit tests without sockets or TLS.
3. Connection-loop integration behind packet protection seams.
4. Non-production loopback proof.
5. Future adapter boundary for real TLS and packet protection.

The current baseline is a testable QUIC-like core with an explicit crypto adapter seam. Real TLS 1.3, AEAD, header protection, congestion control, and production interoperability are explicitly deferred.

## Getting Started

### Requirements

- Windows with Visual Studio 18 2026 generator, or an equivalent CMake C++20 toolchain.
- CMake 3.25 or newer.
- vcpkg with `VCPKG_ROOT` set.

### Configure

```powershell
$env:VCPKG_ROOT = "D:/vcpkg"
cmake --preset windows-msvc-vcpkg
```

### Build

```powershell
$env:VCPKG_ROOT = "D:/vcpkg"
cmake --build --preset windows-msvc-vcpkg
```

### Test

```powershell
ctest --preset windows-msvc-vcpkg --timeout 10
```

## Project Navigation

- `include/flowq/`: public library headers.
- `include/flowq/asio.hpp`: Asio sender-style boundary helpers.
- `include/flowq/quic/core.hpp`: deterministic protocol action seam.
- `include/flowq/quic/varint.hpp`: QUIC varint codec.
- `include/flowq/quic/frame.hpp`: structural frame codec.
- `include/flowq/quic/packet_header.hpp`: structural packet header codec.
- `include/flowq/quic/packet_pipeline.hpp`: packet assembly/parsing through protection seams.
- `include/flowq/quic/ack_loss.hpp`: ACK/loss and recovery primitives.
- `include/flowq/quic/stream.hpp`: stream receive/send state and flow-control signals.
- `include/flowq/quic/connection.hpp`: deterministic connection loop integration.
- `tests/integration/`: deterministic in-memory loopback tests that compose connection-loop pieces.
- `tests/unit/`: Catch2 tests for each protocol module.
- `docs/development.md`: implemented milestone notes and build guidance.
- `docs/superpowers/specs/`: design documents for staged milestones.
- `docs/superpowers/plans/`: execution checklists for staged milestones.
- `PLAN.md`: top-level roadmap and risk register.

## Tech Stack

- **C++20** for value-oriented protocol code and strong type modeling.
- **CMake presets** for repeatable configure/build/test commands.
- **vcpkg** for dependency management.
- **standalone Asio** for future UDP/timer integration boundaries.
- **stdexec** for sender/receiver-oriented execution direction.
- **Catch2** for deterministic unit tests.

## Contribution Guide

- Follow the milestone docs in `docs/superpowers/plans/` and keep `PLAN.md` plus `docs/development.md` synchronized.
- Use TDD for feature and bug work: add a focused failing test, verify RED, implement minimal GREEN, then refactor.
- Keep changes atomic: implementation and direct tests together; docs in a separate commit when practical.
- Do not introduce production TLS, AEAD, header protection, or security claims without an explicit adapter milestone.
- Run the build and full CTest suite before considering work complete.

## Current Status

M19 is complete: FlowQ now labels plaintext packet protection as test-only and rejects it on paths that explicitly require production packet protection. The codebase remains non-production QUIC-like infrastructure; real TLS, AEAD, header protection, and interoperability are future adapter work.
