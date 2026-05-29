# FlowQ Architecture

## Design Overview

FlowQ is a modern C++20 QUIC-like protocol library that builds deterministic protocol primitives first, then layers connection behavior and loopback tests without claiming production QUIC security.

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
- **frame.hpp**: QUIC frame codec (STREAM, ACK, CRYPTO, etc.)
- **packet_header.hpp**: Long/short header codecs

### Connection Management

- **connection.hpp**: Deterministic connection loop with packet space management
- **session.hpp**: Public session façade over connection loop
- **stream.hpp**: Stream receive/send state with flow control
- **recovery_scheduler.hpp**: ASIO sender for deterministic recovery timers
- **lifecycle_scheduler.hpp**: ASIO sender for idle, closing, and draining lifecycle timers
- **timer_scheduler.hpp**: Unified ASIO sender that selects the earliest recovery or lifecycle timer

### Recovery and Congestion

- **ack_loss.hpp**: ACK/loss detection, RTT estimation, PTO
- **congestion.hpp**: NewReno-style congestion controller

### Security Boundaries

- **crypto_provider.hpp**: External crypto provider capability evidence
- **tls_handshake.hpp**: TLS handshake adapter boundary
- **key_lifecycle.hpp**: Key availability and packet-space discard state
- **tls_provider_backend.hpp**: OpenSSL QUIC TLS provider surface

### Diagnostics

- **diagnostics.hpp**: Event sink for qlog-style observability

### HTTP/3

- **http3.hpp**: HTTP/3 frame encoding (DATA, HEADERS, SETTINGS, GOAWAY)

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

Unit tests use plaintext protection and deterministic timers. No real crypto, no network, no randomness.

### Virtual Seams

External dependencies (TLS, crypto, diagnostics) use virtual interfaces:
- `packet_protector` for packet protection
- `tls_handshake_adapter` for TLS
- `diagnostic_sink` for observability

### Fail-Closed Security

Production protection policy rejects test-only protectors. Crypto provider boundaries fail when backend is absent.

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
| FLOWQ_BUILD_EXAMPLES | ON | Build examples |
| FLOWQ_BUILD_FUZZ | OFF | Build fuzz targets |
| FLOWQ_BUILD_INTEROP | OFF | Build interop harness |
| FLOWQ_ENABLE_OPENSSL_QUIC_TLS | OFF | Enable OpenSSL QUIC TLS |

## Testing Strategy

### Unit Tests

Test individual modules in isolation with deterministic inputs.

### Integration Tests

Test module interactions with in-memory loopback.

### Interop Tests

Opt-in tests against mature QUIC implementations.

### Fuzz Tests

Robustness testing with random inputs.

## Future Directions

- **0-RTT**: Early data support (partially implemented)
- **HTTP/3**: Full request/response semantics
- **WebTransport**: HTTP/3-based transport protocol
- **Production TLS**: Real TLS 1.3 integration
- **Congestion Control**: BBR, CUBIC alternatives
