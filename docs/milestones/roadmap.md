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
- The aioquic runner has no skip path; a requested scenario must pass or fail with recorded evidence.
- The aioquic runner fail-closed behavior is covered by the Windows `flowq.aioquic_runner_script` CTest.
- The interop evidence records peer name, peer version, FlowQ TLS backend version, negotiated cipher suite, scenario result, and FlowQ commit.
- `scripts/validate-interop-evidence.py` validates checked-in JSON evidence in the release-readiness gate.

Primary files:

- `tests/interop/test_interop.py`
- `tests/interop/aioquic_peer.py`
- `tools/quic_client.cpp`
- `tests/interop/ngtcp2_initial_smoke.cpp`
- `scripts/run-aioquic-interop.ps1`
- `scripts/validate-interop-evidence.py`

## Package Boundary

Current evidence:

- `cmake --install` exports `FlowQ::flowq`, `FlowQConfig.cmake`, and `FlowQConfigVersion.cmake`.
- Install validation rejects HTTP/3, QPACK, 0-RTT, and interop test-support headers in the installed package.
- Experimental examples are outside the default build and require `FLOWQ_BUILD_EXPERIMENTAL_EXAMPLES=ON`.

Primary files:

- `CMakeLists.txt`
- `scripts/validate-build.ps1`
- `scripts/validate-build.sh`
- `tests/package-consumer/`

## Remaining Production Gates

The authoritative status is `docs/production/readiness-gate.md` and `docs/production/release-checklist.md`. Current open gates are a second external full-flow peer, human security review, and external security audit. ngtcp2 Initial packet generation smoke is available through the vcpkg-backed interop preset and remains outside the full-flow evidence gate.
