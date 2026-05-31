# FlowQ Evidence Index

This file maps the current source tree to production-gate evidence.

## Transport Core

Current evidence:

- QUIC varint, packet-number, frame, packet-header, packet-pipeline, transport-parameter, ACK/loss, stream, flow-control, recovery, congestion, routing, retry, lifecycle, and endpoint-driver tests are part of `flowq_unit_tests`.
- Deterministic in-memory loopback tests are part of the default CTest suite.
- Package-consumer verification proves the installed CMake package can be consumed outside the monorepo target graph.

Primary files:

- `include/flowq/quic/varint.hpp`
- `include/flowq/quic/frame.hpp`
- `include/flowq/quic/packet_header.hpp`
- `include/flowq/quic/packet_pipeline.hpp`
- `include/flowq/quic/transport_parameters.hpp`
- `include/flowq/quic/connection.hpp`
- `include/flowq/quic/session.hpp`
- `include/flowq/quic/endpoint_driver.hpp`

## Crypto and TLS Boundary

Current evidence:

- Packet protection requires a production-capable external crypto provider by default.
- Test-only plaintext protection lives under `tests/support/`.
- OpenSSL AES-128-GCM and RFC 9001 Initial-vector coverage are gated by backend build options.
- OpenSSL QUIC TLS server configuration fails when certificate chain or private key configuration is absent or invalid.
- TLS backend versions and negotiated cipher suites are recorded by interop runs.

Primary files:

- `include/flowq/quic/crypto_provider.hpp`
- `include/flowq/quic/openssl_aead_protector.hpp`
- `include/flowq/quic/initial_keys.hpp`
- `include/flowq/quic/openssl_tls_handshake.hpp`
- `include/flowq/quic/tls_protector_factory.hpp`

## Interop

Current evidence:

- aioquic from the `expr` conda environment observes FlowQ handshake completion, and the direct Python `bidirectional_stream` and `loss_recovery` scenarios pass.
- External interop wrapper scripts fail when a requested scenario is skipped.
- The interop harness records peer name, peer version, FlowQ TLS backend version, negotiated cipher suite, and scenario result.

Primary files:

- `tests/interop/test_interop.py`
- `tests/interop/aioquic_peer.py`
- `tests/interop/flowq_endpoint_driver.cpp`
- `tests/interop/scenarios/`
- `scripts/run-interop.ps1`
- `scripts/run-interop.sh`

## Package Boundary

Current evidence:

- `cmake --install` exports `FlowQ::flowq`, `FlowQConfig.cmake`, and `FlowQConfigVersion.cmake`.
- Install validation rejects HTTP/3, QPACK, 0-RTT, and interop test-support headers in the installed package.
- Source-only examples are outside the default build and require `FLOWQ_BUILD_SOURCE_ONLY_EXAMPLES=ON`.

Primary files:

- `CMakeLists.txt`
- `scripts/validate-build.ps1`
- `scripts/validate-build.sh`
- `tests/package-consumer/`

## Remaining Production Gates

The current release checklist still requires Linux CTest evidence, sanitizer evidence, and human security review before public production-candidate wording can be claimed. The authoritative status is `docs/production/readiness-gate.md` and `docs/production/release-checklist.md`.
