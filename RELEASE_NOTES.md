# FlowQ Release Notes

## Version 1.0.0 (2026-06-01)

Production hardening release with multi-cipher support, congestion control algorithms, and interop validation.

### Features

#### Security
- **Key material erasure**: All traffic secrets, AEAD keys, and IVs are securely erased on destruction
- **Multi-cipher AEAD support**: AES-128-GCM, AES-256-GCM, ChaCha20-Poly1305
- **Cipher-suite-aware header protection**: AES-ECB for AES-128/256-GCM, ChaCha20 for ChaCha20-Poly1305
- **AEAD key rotation**: RFC 9000 Section 6 key update support
- **Traffic secret access restriction**: `traffic_secret()` only available with `FLOWQ_ENABLE_INSPECTION`

#### Congestion Control
- **BBR**: Bottleneck Bandwidth and Round-trip propagation time
- **CUBIC**: RFC 8312 cubic congestion control
- **Pacing**: RFC 9002 Section 7.7 send rate smoothing
- **Configurable algorithms**: Select congestion algorithm via `connection_loop_config`

#### Interop
- **aioquic 1.3.0**: Handshake, bidirectional stream, loss recovery (all PASS)
- **ngtcp2 1.20.0**: Initial packet generation (PASS)

#### Testing
- **514 unit tests** passing
- **Benchmark framework**: 40 scenarios across 4 categories
- **Soak test**: 10,000 connections, 0 errors, 830 conn/sec
- **Zero-copy packet builder**: Single-buffer assembly (experimental)

### Breaking Changes

- Default congestion algorithm is NewReno (unchanged)
- Pacing disabled by default (`enable_pacing = false`)
- Zero-copy disabled by default (`enable_zero_copy = false`)
- `traffic_secret()` requires `FLOWQ_ENABLE_INSPECTION`

### Known Limitations

- Single cipher suite per connection (no negotiation)
- No HTTP/3, QPACK, WebTransport (source-only)
- No 0-RTT deployment support
- Windows-only validation (Linux/macOS pending)

### Upgrade Guide

No breaking API changes from 0.1.0. All existing code should compile unchanged.

To enable new features:
```cpp
flowq::quic::connection_loop_config config;
config.congestion_algo = flowq::quic::congestion_algorithm::bbr;  // or cubic
config.enable_pacing = true;
config.enable_key_update = true;
```

---

## Version 0.1.0 (2026-05-29)

Initial release with QUIC v1 transport core.

### Capabilities

- QUIC value codecs: varint, packet number, packet header, frame, and transport parameter handling.
- Packet pipeline: assembly/parsing through explicit packet-protection interfaces.
- Packet protection: OpenSSL-gated AES-128-GCM packet protection with RFC 9001 header protection.
- Fail-closed behavior: OpenSSL AEAD creation fails when the crypto backend is not compiled in.
- Connection loop: packet-space tracking, ACK/loss integration, stream delivery, flow-control updates.
- Path validation primitives: PATH_CHALLENGE/PATH_RESPONSE codec support.
- Recovery and congestion: deterministic recovery timers, bytes-in-flight accounting, NewReno-style baseline.
- Endpoint surfaces: session facade, UDP/ASIO adapter, endpoint driver, connection ID routing.
- Robustness: fuzz targets, sanitizer workflow, package-consumer check.
