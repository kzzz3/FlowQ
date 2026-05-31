# FlowQ Architecture

## Design Overview

FlowQ is a C++20 QUIC transport library under production hardening. The current architecture combines deterministic protocol primitives, connection-loop behavior, packet-protection seams, OpenSSL-gated AES-128-GCM packet protection, local endpoint surfaces, diagnostics, release-gate tooling, and recorded aioquic handshake, stream, and loss-recovery interop evidence. Production-candidate status is gated on the remaining release evidence and human review.

## Architecture Layers

```
┌─────────────────────────────────────────────────────────────┐
│                    Public API Layer                          │
│  session.hpp, udp_session.hpp, endpoint_driver.hpp          │
├─────────────────────────────────────────────────────────────┤
│                  Connection Layer                            │
│  connection.hpp, session.hpp, timer schedulers              │
├─────────────────────────────────────────────────────────────┤
│                  Protocol Layer                              │
│  packet_pipeline.hpp, ack_loss.hpp, congestion.hpp          │
├─────────────────────────────────────────────────────────────┤
│                  Codec Layer                                 │
│  frame.hpp, packet_header.hpp, varint.hpp                   │
├─────────────────────────────────────────────────────────────┤
│                  Value Layer                                 │
│  buffer.hpp, error.hpp, endpoint.hpp                        │
└─────────────────────────────────────────────────────────────┘
```

## Core Components

### Value Types

- **buffer**: Owned byte buffer
- **error**: Error code + message
- **endpoint**: Host, port, ALPN

### Protocol Codecs

- **varint.hpp**: QUIC variable-length integer encoding
- **frame.hpp**: QUIC frame codec (STREAM, ACK, CRYPTO, PATH_CHALLENGE, PATH_RESPONSE, etc.)
- **packet_header.hpp**: Long/short header codecs
- **transport_parameters.hpp**: QUIC transport parameter codec and config mapping
- **openssl_aead_protector.hpp**: OpenSSL-gated AES-128-GCM packet protection and header protection

### Connection Management

- **connection.hpp**: Deterministic connection loop with packet space management
- **session.hpp**: Public session façade over connection loop
- **stream.hpp**: Stream receive/send state with flow control
- **recovery_scheduler.hpp**: ASIO sender for deterministic recovery timers
- **lifecycle_scheduler.hpp**: ASIO sender for idle, closing, and draining lifecycle timers
- **timer_scheduler.hpp**: Unified ASIO sender that selects the earliest recovery or lifecycle timer

### Recovery and Congestion

- **ack_loss.hpp**: ACK/loss detection, RTT estimation, PTO
- **congestion.hpp**: NewReno-style congestion controller plus structural BBR/CUBIC controllers

### Security Boundaries

- **crypto_provider.hpp**: External crypto provider capability evidence
- **tls_handshake.hpp**: TLS handshake adapter boundary
- **key_lifecycle.hpp**: Key availability and packet-space discard state
- **tls_provider_backend.hpp**: OpenSSL QUIC TLS backend status

### Diagnostics

- **diagnostics.hpp**: Event sink for qlog-style observability

### Source-Only Codecs

- **http3.hpp / http3_request.hpp / qpack.hpp**: HTTP/3 and QPACK codecs kept in source and test coverage. They are excluded from the production install package and remain outside the production-candidate API scope.
- **zero_rtt.hpp**: Early-data state helpers kept in source and test coverage. The header is excluded from the production install package and 0-RTT deployment policy remains outside the production-candidate API scope.

## Packet Number Spaces

FlowQ maintains separate state for three packet number spaces:

- **Initial**: Handshake establishment
- **Handshake**: TLS handshake completion
- **Application**: Post-handshake data

Each space has independent:
- Packet number counters
- Sent/received packet trackers
- ACK state
- Recovery timers

## Key Design Decisions

### Header-Only Library

All code is in headers for easy integration. No separate compilation units.

### Deterministic Testing

Unit tests use deterministic timers and local packet-protection seams. OpenSSL-enabled tests cover AES-128-GCM packet protection; plaintext packet protection lives only in test support, and production-required policy rejects test-only protectors.

### Virtual Seams

External dependencies (TLS, crypto, diagnostics) use virtual interfaces:
- `packet_protector` for packet protection
- `tls_handshake_adapter` for TLS
- `diagnostic_sink` for observability

### Fail-Closed Security

Production protection policy rejects test-only protectors. Crypto provider boundaries fail when backend is absent, OpenSSL AEAD creation fails when the crypto backend is not compiled in, and OpenSSL QUIC TLS server construction fails when the certificate chain or private key is absent, unreadable, or mismatched.

## Build System

### CMake Presets

- `windows-msvc-vcpkg`: Default Windows build
- Custom presets for different configurations

### vcpkg Integration

Dependencies managed via `vcpkg.json` manifest:
- asio (standalone)
- catch2
- stdexec
- openssl (optional)

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| FLOWQ_BUILD_TESTS | ON | Build tests |
| FLOWQ_BUILD_SOURCE_ONLY_EXAMPLES | OFF | Build source-only examples outside the production-candidate API |
| FLOWQ_BUILD_FUZZ | OFF | Build fuzz targets |
| FLOWQ_BUILD_INTEROP | OFF | Build interop harness |
| FLOWQ_ENABLE_OPENSSL_QUIC_TLS | OFF | Enable OpenSSL QUIC TLS backend |
| FLOWQ_ENABLE_OPENSSL_CRYPTO | OFF | Enable OpenSSL-backed packet-protection primitives |

## Testing Strategy

### Unit Tests

Test individual modules in isolation with deterministic inputs.

### Integration Tests

Test module interactions with in-memory loopback.

### Interop Tests

Opt-in tests target external QUIC implementations. Production-candidate wording requires recorded peer names, versions, scenarios, and results.

### Fuzz Tests

Robustness testing with random inputs.

## Production-Candidate Boundary

- aioquic 1.3.0 interop results are recorded for handshake, bidirectional stream echo, and application loss recovery.
- Human security review is not recorded.
- ChaCha20-Poly1305 and AES-256-GCM packet protection are rejected by `openssl_aead_protector`.
- Live AEAD key update installation is outside current evidence.
- Full path migration, stateless reset, HTTP/3 deployment, WebTransport deployment, and 0-RTT deployment policy are outside the production-candidate scope.
- Source-only HTTP/3, QPACK, and 0-RTT headers are not installed by the production package; the install validation gate fails if they reappear in `build/install-flowq/include`.
- Source-only examples are not part of the default build; they require `FLOWQ_BUILD_SOURCE_ONLY_EXAMPLES=ON`.
