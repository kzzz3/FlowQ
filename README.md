# FlowQ

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![CMake](https://img.shields.io/badge/build-CMake-informational)
![Tests](https://img.shields.io/badge/tests-CMake%2FCTest-green)
![QUIC](https://img.shields.io/badge/protocol-QUIC%20v1-informational)
![TLS](https://img.shields.io/badge/TLS-1.3-blue)
![OpenSSL](https://img.shields.io/badge/crypto-OpenSSL-orange)

FlowQ is a C++20 QUIC transport library under production hardening. The current codebase provides deterministic QUIC transport primitives, connection-loop behavior, stream and flow-control state, recovery/congestion primitives, routing/retry helpers, OpenSSL-gated AES-128-GCM packet protection, provider-backed TLS adapter surfaces, and local session/endpoint adapters. It does not carry a production-candidate claim until external peer interop evidence and human review are recorded.

## Features

- **QUIC v1 transport core**: value codecs, packet pipeline, streams, ACK/loss, flow control, routing, and timers.
- **TLS 1.3 adapter surface**: OpenSSL 3.5+ QUIC TLS via `SSL_set_quic_tls_cbs()` when enabled.
- **AEAD Protection**: AES-128-GCM with RFC 9001 header protection
- **Production policy gate**: test-only packet protection is rejected when production protection is required.
- **Interop harness**: process-driven scripts and harness wiring for external peer validation.

## Documentation

📖 **[Full Documentation](docs/README.md)** - Complete documentation index

### Quick Links

- [Getting Started](docs/guides/getting-started.md) - First steps with FlowQ
- [Building](docs/guides/building.md) - Build instructions and options
- [Testing](docs/guides/testing.md) - Running and writing tests
- [Architecture](docs/reference/architecture.md) - System design overview
- [API Reference](docs/api/html/index.html) - Generated API documentation (run `scripts/generate-docs.sh` to generate)

## Current Architecture

FlowQ is organized around explicit protocol boundaries:

1. Value codecs for varints, frames, packet headers, transport parameters, and packet numbers.
2. Deterministic connection-loop state for packet spaces, streams, ACK/loss, recovery, congestion, routing, retry, and lifecycle timers.
3. Packet protection seams that reject test-only protectors when production protection is required.
4. OpenSSL-gated AES-128-GCM packet protection and RFC 9001 header protection when `FLOWQ_ENABLE_OPENSSL_CRYPTO` is enabled.
5. Public session, UDP/ASIO, endpoint-driver, diagnostics, fuzz, package-consumer, and CI surfaces.
6. Structural HTTP/3, QPACK, WebTransport, 0-RTT, BBR, and CUBIC modules that are documented outside the production-candidate scope.

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

These examples demonstrate deterministic in-memory stream exchange, local UDP session wiring, and packet-protection policy behavior showing that plaintext/test-only protection is rejected when production protection is required.

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
- `include/flowq/quic/openssl_aead_protector.hpp`: OpenSSL-backed AEAD packet protector (AES-128-GCM and header protection) — gated behind `FLOWQ_ENABLE_OPENSSL_CRYPTO`.
- `include/flowq/quic/ack_loss.hpp`: ACK/loss and recovery primitives.
- `include/flowq/quic/stream.hpp`: stream receive/send state and flow-control signals.
- `include/flowq/quic/endpoint_driver.hpp`: production-shaped endpoint driver with explicit lifecycle, CID routing integration, and connection limits.
- `include/flowq/quic/connection_routing.hpp`: deterministic connection ID routing table, version negotiation, and retry interface helpers.
- `include/flowq/quic/congestion.hpp`: deterministic bytes-in-flight accounting and NewReno-style congestion controller.
- `include/flowq/quic/connection.hpp`: deterministic connection loop integration.
- `include/flowq/quic/events.hpp`: public session façade result and stream-delivery values.
- `include/flowq/quic/session.hpp`: synchronous public QUIC session façade over the deterministic connection loop.
- `include/flowq/quic/udp_session.hpp`: UDP/ASIO adapter for local session integration.
- `include/flowq/quic/recovery_scheduler.hpp`: ASIO scheduling adapter for deterministic QUIC recovery timer values.
- `include/flowq/quic/lifecycle_scheduler.hpp`: ASIO scheduling adapter for idle, closing, and draining lifecycle timers.
- `include/flowq/quic/timer_scheduler.hpp`: unified ASIO scheduling adapter that selects the earliest recovery or lifecycle timer.
- `examples/in_memory_loopback.cpp`: deterministic in-memory session façade example.
- `examples/udp_stream_echo.cpp`: local UDP session example.
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
- **standalone Asio** for UDP/timer integration boundaries.
- **stdexec** for sender/receiver-oriented execution direction.
- **Catch2** for deterministic unit tests.

## Contribution Guide

- Keep implementation, tests, and production-gate documentation synchronized.
- Use TDD for feature and bug work: add a focused failing test, verify RED, implement minimal GREEN, then refactor.
- Keep changes atomic: implementation and direct tests together; docs in a separate commit when practical.
- Do not claim broader TLS, cipher-suite, peer-interop, or audited-security coverage without matching tests and readiness evidence.
- Run the build and full CTest suite before considering work complete.

## Current Capability Snapshot

- **Transport core**: QUIC varints, frames, packet headers, packet pipeline, packet-number helpers, transport parameters, ACK/loss, stream state, and flow-control frames.
- **Connection behavior**: deterministic packet-space handling, connection lifecycle, recovery timers, congestion accounting, connection ID routing, version negotiation, retry helpers, and endpoint-driver lifecycle.
- **Packet protection**: OpenSSL-backed AES-128-GCM packet protection with header protection when `FLOWQ_ENABLE_OPENSSL_CRYPTO` is enabled; unsupported cipher suites are rejected; creation fails closed when the OpenSSL crypto backend is not compiled in.
- **Path validation primitives**: PATH_CHALLENGE/PATH_RESPONSE codec support and Application-space same-value response scheduling.
- **Public surfaces**: session facade, UDP/ASIO adapter, timer schedulers, diagnostics, examples, CMake package export, package-consumer check, fuzz targets, and sanitizer CI.
- **Experimental surfaces**: HTTP/3/QPACK, WebTransport, 0-RTT, BBR, and CUBIC are structural modules and are not part of the production-candidate scope.

The plaintext protector remains test-only and is rejected by production-required packet-protection policy.

The test suite covers protocol core, RFC compliance, integration, performance, fuzz, and AEAD modules.

FlowQ is **non-production**. A production-candidate claim requires recorded peer interop results, release-gate evidence, and human review.

See `docs/production/readiness-gate.md` for the current production-candidate gate.

### Production Hardening

The following hardening measures are in place:

- **Fuzz targets**: `fuzz_packet_header`, `fuzz_frame_decode`, `fuzz_qpack` with `LLVMFuzzerTestOneInput` entry points
- **Sanitizer CI**: ASan + UBSan gates in `.github/workflows/robustness.yml` with `-fno-omit-frame-pointer` for full stack traces
- **noexcept move semantics**: `stream_receive_state`, `stream_send_state`, `connection_loop`, and `buffer` all have explicit `noexcept` move constructors and assignment operators
- **constexpr utilities**: `max_varint`, `encoded_size`, `default_initial_window()`, `default_minimum_window()` are compile-time evaluable
- **API hardening**: `detail::` namespaces gated, inspection methods behind `FLOWQ_ENABLE_INSPECTION`, `[[nodiscard]]` on value-returning methods
- **Documentation**: ownership/lifetime `@pre` comments on raw pointer members, thread-safety contracts on session/connection/endpoint types, and explicit scope boundaries for structural HTTP/3/WebTransport surfaces
