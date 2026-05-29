# FlowQ Release Notes

## Current Snapshot

- **Date**: 2026-05-29
- **Status**: Non-production; external peer interop evidence and human review are not recorded.
- **Tests**: CMake/CTest suite.

## Current Capabilities

- QUIC value codecs: varint, packet number, packet header, frame, and transport parameter handling.
- Packet pipeline: assembly/parsing through explicit packet-protection interfaces.
- Packet protection: OpenSSL-gated AES-128-GCM packet protection with RFC 9001 header protection; unsupported cipher suites are rejected.
- Fail-closed behavior: OpenSSL AEAD creation fails when the crypto backend is not compiled in; plaintext/test protection is rejected when production protection is required.
- Connection loop: packet-space tracking, ACK/loss integration, stream delivery, flow-control updates, lifecycle timers, and deterministic outbound actions.
- Path validation primitives: PATH_CHALLENGE/PATH_RESPONSE codec support and Application-space same-value response scheduling.
- Recovery and congestion: deterministic recovery timers, bytes-in-flight accounting, NewReno-style baseline, and structural BBR/CUBIC controllers.
- Endpoint surfaces: session facade, UDP/ASIO smoke adapter, endpoint driver, connection ID routing, version negotiation, retry helpers, and diagnostics.
- Robustness: fuzz targets, sanitizer workflow, package-consumer check, and release-readiness scripts.
- Structural extension modules: HTTP/3/QPACK, WebTransport, and 0-RTT surfaces are present but outside the production-candidate scope.

## Current Gaps

- External peer QUIC interop results are not recorded.
- Human security review is not recorded.
- TLS certificate-validation policy is not production-gated.
- ChaCha20-Poly1305 and AES-256-GCM packet protection are not implemented by `openssl_aead_protector`.
- Live AEAD key update installation is not implemented.
- Full path migration, stateless reset, 0-RTT deployment policy, HTTP/3 server deployment, and WebTransport deployment are outside the current production-candidate scope.
