# FlowQ Basic Complete Baseline

FlowQ is basic complete as a non-production C++20 QUIC-like library baseline. The baseline is intended for local protocol experimentation, deterministic tests, public API exploration, examples, and package-consumption validation. It is not a production QUIC implementation and does not claim wire interoperability or production security.

## Supported Today

- Public header-only library surface under `include/flowq/`.
- QUIC-like structural value types for varints, frames, packet headers, ACK/loss state, recovery timing values, stream send/receive state, and connection-loop integration.
- Synchronous public session façade in `flowq::quic::session` for queueing stream data, flushing outbound datagrams, processing inbound datagrams, observing stream deliveries, acknowledging packets, and exposing deterministic recovery timer values.
- Explicit packet-protection seam through `flowq::quic::packet_protector` and `flowq::quic::packet_protection_policy`.
- External crypto-provider capability evidence values for future vetted provider adapters.
- Optional OpenSSL-backed RFC 9001 Initial vector helpers when the explicit crypto backend option is enabled.
- Structural QUIC transport parameter encode/decode helpers and config mapping for selected flow-control and connection-policy values.
- TLS handshake adapter boundary values for opaque CRYPTO byte movement, handshake-state observation, and key-availability gating.
- Default-off OpenSSL QUIC TLS provider metadata/API-availability surface when explicitly enabled and supported by the local OpenSSL package.
- RFC-shaped short-header value model and test-mode parser shell that keep protected payload bytes opaque and separate from the structural Application envelope.
- Deterministic key availability and packet-space discard gates for Initial, Handshake, 0-RTT value state, and 1-RTT value state.
- Plaintext packet protection for deterministic tests and local examples, marked test-only by capability reporting.
- Bounded non-production UDP/ASIO session adapter for local loopback smoke paths with caller-owned sockets.
- ASIO recovery scheduler adapter that schedules already-computed deterministic recovery deadlines.
- Buildable examples covering in-memory loopback, bounded local UDP loopback, and packet-protection policy behavior.
- CMake install/export packaging with the `FlowQ::flowq` target and a separate package-consumer smoke project.
- Windows MSVC/vcpkg CI workflow that builds, tests, installs, and verifies package consumption.

## Explicitly Not Supported

- TLS 1.3 handshake, certificate validation, key schedule, or key update.
- QUIC AEAD packet protection, header protection, or authenticated production packet encryption.
- RFC-valid 1-RTT short-header packet format, packet-number truncation, packet-number reconstruction, or key phase handling.
- Congestion control, pacing, ECN, persistent congestion, ACK frequency, migration, Retry integrity, stateless reset, or path validation.
- DNS resolution, connection establishment policy, listener demultiplexing, or production UDP transport semantics.
- HTTP/3, WebTransport, QPACK, or application-layer protocol support.
- Interoperability with real QUIC endpoints.
- Any security guarantee from `plaintext_packet_protector`; it is for deterministic tests and local smoke examples only.

## Public Headers

- `include/flowq/buffer.hpp`
- `include/flowq/context.hpp`
- `include/flowq/endpoint.hpp`
- `include/flowq/error.hpp`
- `include/flowq/execution.hpp`
- `include/flowq/asio/timer.hpp`
- `include/flowq/asio/udp.hpp`
- `include/flowq/quic/ack_loss.hpp`
- `include/flowq/quic/connection.hpp`
- `include/flowq/quic/core.hpp`
- `include/flowq/quic/crypto_provider.hpp`
- `include/flowq/quic/events.hpp`
- `include/flowq/quic/frame.hpp`
- `include/flowq/quic/initial_keys.hpp`
- `include/flowq/quic/key_lifecycle.hpp`
- `include/flowq/quic/packet_header.hpp`
- `include/flowq/quic/packet_pipeline.hpp`
- `include/flowq/quic/recovery_scheduler.hpp`
- `include/flowq/quic/session.hpp`
- `include/flowq/quic/stream.hpp`
- `include/flowq/quic/tls_handshake.hpp`
- `include/flowq/quic/tls_provider_backend.hpp`
- `include/flowq/quic/transport_parameters.hpp`
- `include/flowq/quic/udp_session.hpp`
- `include/flowq/quic/varint.hpp`

## Examples

- `flowq_example_in_memory_loopback`: two `flowq::quic::session` objects exchange one structural Application stream payload entirely in memory.
- `flowq_example_udp_stream_echo`: two local UDP sockets exchange one structural Application stream payload through `flowq::quic::udp_session`.
- `flowq_example_protection_policy`: shows plaintext/test-only protection accepted under `test_allowed` and rejected under `production_required`.

These examples are smoke examples for the current library surface. They are not production QUIC examples, not security examples, and not interoperability tests.

## Build and Test Gate

Local verification on Windows with vcpkg:

```powershell
$env:VCPKG_ROOT = "D:/vcpkg"
cmake --preset windows-msvc-vcpkg
cmake --build --preset windows-msvc-vcpkg
ctest --preset windows-msvc-vcpkg --timeout 10
cmake --install build/windows-msvc-vcpkg --config Debug --prefix build/install-flowq
cmake -S tests/package-consumer -B build/package-consumer -G "Visual Studio 18 2026" -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DCMAKE_PREFIX_PATH="F:/Project/FlowQ/build/install-flowq"
cmake --build build/package-consumer --config Debug
.\build\package-consumer\Debug\flowq_package_consumer.exe
```

The CI workflow in `.github/workflows/ci.yml` runs the same gate shape on a Windows MSVC/vcpkg runner: configure, build, CTest, install, external package-consumer configure/build, and package-consumer execution.

## Future Production QUIC Backlog

Production QUIC work remains intentionally separate from this baseline and requires new specs and plans before implementation:

- RFC 9000 packet-number truncation/reconstruction helpers, the M28 crypto-provider capability boundary, selected M29
  RFC 9001 Initial vectors, the M30 structural transport-parameter codec, the M31 TLS handshake adapter boundary, the
  M31b-a default-off OpenSSL QUIC TLS provider surface, the M32 short-header shell, and M33 key lifecycle gates are in
  place, but complete TLS handshakes, certificate validation, key schedule, key updates, authenticated transport-parameter
  negotiation, complete packet protection, header-protection removal, 1-RTT AEAD, and interoperability remain future work.
- The complete M28-M39 production-readiness route is tracked in
  `docs/superpowers/plans/2026-04-27-post-basic-production-readiness-roadmap.md`.
- External TLS 1.3 and QUIC packet-protection adapter backed by a mature crypto library.
- RFC-valid 1-RTT short-header encoding, packet-number truncation/reconstruction, key phase, and key update.
- Congestion control, pacing, ECN, persistent congestion, ACK frequency, and production recovery policy.
- Address validation, Retry integrity, stateless reset, connection migration, and path validation.
- Interoperability tests against real QUIC endpoints.
- HTTP/3 and WebTransport layers.
