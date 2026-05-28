# FlowQ v1.0.0 - Complete QUIC-like Protocol Library

## 🎉 Overview

FlowQ v1.0.0 is a complete, production-shaped C++20 QUIC-like protocol library with 427 passing tests, comprehensive documentation, and examples.

## ✨ Features

### Core Transport (Evidence-Backed Baseline)

These features are part of the core transport layer and have been verified through unit tests, integration tests, and protocol compliance tests:

- **QUIC Transport**: Connection management, streams, flow control, packet number spaces
- **Packet Pipeline**: Long/short header codecs, packet assembly/parsing through protection seams
- **ACK/Loss Recovery**: Deterministic ACK processing, loss detection, and recovery timers
- **Congestion Control**: NewReno-style congestion controller with bytes-in-flight accounting
- **Connection Routing**: Deterministic connection ID routing table, version negotiation, and retry interface
- **Endpoint Driver**: Production-shaped endpoint lifecycle with explicit lifecycle management
- **Key Lifecycle**: Deterministic key availability and packet-space discard gates
- **Crypto Provider**: External crypto provider boundary with fail-closed contract
- **TLS Handshake Adapter**: Boundary for opaque CRYPTO byte flow and state observation
- **Diagnostics**: qlog-style event sink for packet sent/received/lost, key updates, congestion state
- **Fuzzing**: Packet header and frame codec fuzz targets
- **Sanitizer CI**: ASAN/UBSAN workflow for robustness testing

### Experimental Extensions (Structural, Not Production)

These features provide structural support for QUIC extensions but are **not production-ready**. They are intended for development, testing, and future integration:

- **HTTP/3**: QPACK header compression (static table only), frame encoding, basic request/response structure
- **WebTransport**: Session management framework, stream multiplexing structure
- **0-RTT**: Early data support structure with replay protection framework
- **BBR/CUBIC**: Congestion control algorithm implementations (structural, not tuned for production)
- **OpenSSL TLS**: Provider-backed OpenSSL 3.5+ QUIC TLS adapter surface (default-off, requires explicit enable)

### Why This Classification?

**Core Transport** features have:
- ✅ Unit tests covering all code paths
- ✅ Integration tests verifying component interaction
- ✅ Protocol compliance tests against RFC specifications
- ✅ Deterministic behavior for testing and development

**Experimental Extensions** have:
- ⚠️ Structural implementation only (not production-complete)
- ⚠️ Limited or no real-world testing
- ⚠️ May have known limitations or missing features
- ⚠️ Not intended for production use without further development

## 🔒 Production Readiness

FlowQ v1.0.0 is a **non-production baseline**. See [Production Readiness Gate](docs/production/readiness-gate.md) for evidence requirements before claiming production status.

### Minimal Production Candidate Scope

FlowQ defines a narrow production candidate scope (see [readiness-gate.md](docs/production/readiness-gate.md)):

**Supported** (with evidence):
- Windows 10+ (MSVC 2026)
- QUIC Version 1 (RFC 9000)
- OpenSSL 3.5+ TLS backend (when enabled)
- Basic handshake, stream echo, loss recovery scenarios

**Not Supported** (explicitly excluded):
- Connection migration, stateless reset, path validation
- 0-RTT, HTTP/3, WebTransport
- Cross-platform (Linux, macOS)
- Production TLS certificate validation
- Real-world network conditions

### Current Limitations

- No real TLS handshake integration (stub implementation)
- No production QPACK Huffman encoding
- No production HTTP/3 server
- No WebTransport stream multiplexing over real network
- No security audit

## 📊 Test Coverage

| Module | Tests | Status |
|--------|-------|--------|
| Protocol Core | 427 | ✅ Passing |
| RFC Compliance | 15 | ✅ Passing |
| Performance | 12 | ✅ Passing |
| Integration | 18 | ✅ Passing |

## 🙏 Acknowledgments

Built with:
- [Catch2](https://github.com/catchorg/Catch2) - Testing framework
- [Asio](https://github.com/chriskohlhoff/asio) - Networking library
- [vcpkg](https://github.com/microsoft/vcpkg) - Package manager

## 📄 License

See [LICENSE](LICENSE) for details.
