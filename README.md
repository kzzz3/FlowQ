# FlowQ

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![CMake](https://img.shields.io/badge/build-CMake-informational)
![Tests](https://img.shields.io/badge/tests-Catch2-green)

FlowQ is a modern C++ QUIC-like protocol library that builds deterministic protocol primitives first, then layers connection behavior and loopback tests without claiming production QUIC security.

## Documentation

📖 **[Full Documentation](docs/README.md)** - Complete documentation index

### Quick Links

- [Getting Started](docs/guides/getting-started.md) - First steps with FlowQ
- [Building](docs/guides/building.md) - Build instructions and options
- [Testing](docs/guides/testing.md) - Running and writing tests
- [Architecture](docs/reference/architecture.md) - System design overview
- [API Reference](docs/api/html/index.html) - Generated API documentation (run `scripts/generate-docs.sh` to generate)

## Design Overview

FlowQ is intentionally staged:

1. Pure value codecs and protocol state machines.
2. Deterministic unit tests without sockets or TLS.
3. Connection-loop integration behind packet protection seams.
4. Non-production loopback proof.
5. Public QUIC library façade, UDP/ASIO adapter, examples, packaging, and CI.
6. Fail-closed provider boundaries for future real TLS and packet protection.

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

### Examples

The default Windows preset builds example executables alongside tests:

- `flowq_example_in_memory_loopback`
- `flowq_example_udp_stream_echo`
- `flowq_example_protection_policy`

These examples demonstrate the current non-production QUIC-like library surface: deterministic in-memory stream exchange, a bounded local UDP smoke path, and packet-protection policy behavior showing that plaintext/test-only protection is rejected when production protection is required. They are not production QUIC, do not provide TLS, AEAD, header protection, congestion control, or interoperability guarantees, and should be treated as local smoke examples only.

### Install and package consumption

FlowQ installs as a CMake package with the `FlowQ::flowq` target:

```powershell
$env:VCPKG_ROOT = "D:/vcpkg"
cmake --build --preset windows-msvc-vcpkg
cmake --install build/windows-msvc-vcpkg --config Debug --prefix build/install-flowq
cmake -S tests/package-consumer -B build/package-consumer -G "Visual Studio 18 2026" -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DCMAKE_PREFIX_PATH="F:/Project/FlowQ/build/install-flowq"
cmake --build build/package-consumer --config Debug
```

The installed package exports headers, `FlowQTargets.cmake`, `FlowQConfig.cmake`, and `FlowQConfigVersion.cmake`. Consumers should use the same vcpkg toolchain so FlowQ's public Asio and stdexec dependencies resolve consistently.

### CI

The GitHub Actions workflow in `.github/workflows/ci.yml` runs the Windows MSVC/vcpkg gate: configure, build, CTest, install, package-consumer configure/build, and package-consumer execution. CI is triggered manually via `workflow_dispatch`.

## Project Navigation

- `include/flowq/`: public library headers.
- `include/flowq/asio/`: Asio sender-style boundary helpers.
- `include/flowq/quic/core.hpp`: deterministic protocol action seam.
- `include/flowq/quic/crypto_provider.hpp`: external crypto-provider capability evidence values.
- `include/flowq/quic/initial_keys.hpp`: optional OpenSSL-backed RFC 9001 Initial vector helpers.
- `include/flowq/quic/key_lifecycle.hpp`: deterministic key availability and packet-space discard state.
- `include/flowq/quic/tls_handshake.hpp`: TLS handshake adapter boundary for opaque CRYPTO byte flow and state observation.
- `include/flowq/quic/tls_provider_backend.hpp`: default-off OpenSSL QUIC TLS provider metadata and API-availability surface.
- `include/flowq/quic/transport_parameters.hpp`: structural QUIC transport parameter codec and config mapping helpers.
- `include/flowq/quic/varint.hpp`: QUIC varint codec.
- `include/flowq/quic/frame.hpp`: structural frame codec.
- `include/flowq/quic/packet_header.hpp`: structural packet header codec.
- `include/flowq/quic/packet_pipeline.hpp`: packet assembly/parsing through protection seams.
- `include/flowq/quic/ack_loss.hpp`: ACK/loss and recovery primitives.
- `include/flowq/quic/stream.hpp`: stream receive/send state and flow-control signals.
- `include/flowq/quic/endpoint_driver.hpp`: production-shaped endpoint driver with explicit lifecycle, CID routing integration, and connection limits.
- `include/flowq/quic/connection_routing.hpp`: deterministic connection ID routing table, version negotiation, and retry interface helpers.
- `include/flowq/quic/congestion.hpp`: deterministic bytes-in-flight accounting and NewReno-style congestion controller.
- `include/flowq/quic/connection.hpp`: deterministic connection loop integration.
- `include/flowq/quic/events.hpp`: public session façade result and stream-delivery values.
- `include/flowq/quic/session.hpp`: synchronous public QUIC session façade over the deterministic connection loop.
- `include/flowq/quic/udp_session.hpp`: bounded non-production UDP/ASIO adapter for local smoke paths.
- `include/flowq/quic/recovery_scheduler.hpp`: ASIO scheduling adapter for deterministic QUIC recovery timer values.
- `examples/in_memory_loopback.cpp`: deterministic in-memory session façade smoke example.
- `examples/udp_stream_echo.cpp`: bounded local UDP session smoke example.
- `examples/protection_policy.cpp`: packet-protection policy example showing plaintext rejection under production-required policy.
- `tests/integration/`: deterministic in-memory loopback tests that compose connection-loop pieces.
- `tests/unit/`: Catch2 tests for each protocol module.
- `.github/workflows/ci.yml`: Windows MSVC/vcpkg CI gate for build, tests, install, and package consumption.
- `.github/workflows/robustness.yml`: Sanitizer and fuzz testing workflow.
- `docs/plan.md`: project roadmap and milestone tracking.
- `docs/guides/`: getting-started, building, and testing guides.
- `docs/production/`: production readiness gate and release checklist.
- `docs/reference/`: architecture documentation.
- `docs/milestones/`: milestone tracking and roadmap.

## Tech Stack

- **C++20** for value-oriented protocol code and strong type modeling.
- **CMake presets** for repeatable configure/build/test commands.
- **vcpkg** for dependency management.
- **standalone Asio** for future UDP/timer integration boundaries.
- **stdexec** for sender/receiver-oriented execution direction.
- **Catch2** for deterministic unit tests.

## Contribution Guide

- Follow the milestone docs in `docs/milestones/` and keep `docs/plan.md` synchronized.
- Use TDD for feature and bug work: add a focused failing test, verify RED, implement minimal GREEN, then refactor.
- Keep changes atomic: implementation and direct tests together; docs in a separate commit when practical.
- Do not introduce production TLS, AEAD, header protection, or security claims without an explicit adapter milestone.
- Run the build and full CTest suite before considering work complete.

## Current Status

All M20-M39 production-readiness milestones are complete. FlowQ has deterministic protocol primitives, TLS handshake adapter boundary, key lifecycle gates, congestion baseline, connection routing, endpoint driver, diagnostics, fuzz targets, and an opt-in interop harness. FlowQ remains a non-production C++20 QUIC-like library baseline until the release checklist in `docs/production/release-checklist.md` is fully satisfied and human security review is completed.

See `docs/production/readiness-gate.md` for the exact evidence required before changing public wording from non-production baseline to production candidate.

All milestones M20-M39 are complete. See `docs/plan.md` for the full milestone list and `docs/milestones/roadmap.md` for the detailed roadmap.
