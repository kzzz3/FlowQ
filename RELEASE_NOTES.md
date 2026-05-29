# FlowQ Release Notes

---

# v1.1.0 - Production Hardening & AEAD Support

**Date**: 2026-05-29
**Status**: Non-production (interop validation pending)
**Tests**: 495 passing

## Overview

FlowQ v1.1.0 completes Phases 0-5 of the production push plan, adding real AEAD packet protection, QPACK fixes, API surface hardening, comprehensive fuzz targets, sanitizer CI gates, and thorough documentation of ownership and thread-safety contracts. The library remains non-production until interop validation against peer QUIC implementations is complete.

## Core Changes

- **AEAD packet protector**: `openssl_aead_protector` implementing `packet_protector` interface with AES-128-GCM and ChaCha20-Poly1305 support, header protection per RFC 9001 §5.4, and key update mechanism — gated behind `FLOWQ_ENABLE_OPENSSL_CRYPTO`
- **QPACK fixes**: Delta-base encoding fix, multi-byte length decoding, dynamic table support, RFC 9204 test vectors
- **API surface hardening**: `detail::` namespaces gated behind `FLOWQ_DETAIL` macro, inspection methods behind `FLOWQ_ENABLE_INSPECTION`, `session_config` consolidation, `[[nodiscard]]` on value-returning methods, dead error codes cleaned up
- **http3.hpp error handling**: `encode_data_frame`, `encode_goaway_frame`, `encode_settings_frame` now return result types instead of silent error swallowing

## Hardening

- **Fuzz targets**: `fuzz_packet_header`, `fuzz_frame_decode`, `fuzz_qpack` with `LLVMFuzzerTestOneInput` entry points, registered in CMakeLists.txt with ASan+UBSan+fuzzer flags
- **Sanitizer CI**: `.github/workflows/robustness.yml` configured with ASan, UBSan, `-fno-omit-frame-pointer` for full stack traces
- **noexcept move semantics**: `stream_receive_state`, `stream_send_state`, `connection_loop`, and `buffer` have explicit `noexcept` move constructors and assignment operators
- **constexpr utilities**: `max_varint`, `encoded_size`, `default_initial_window()`, `default_minimum_window()` verified as compile-time evaluable
- **Threat model**: Documented in `docs/production/readiness-gate.md`

## Documentation

- **Ownership/lifetime**: `@pre` comments on all 14 raw pointer members documenting lifetime requirements
- **Thread-safety contracts**: Documented on `session`, `connection_loop`, `endpoint_driver`, `diagnostics`, `congestion_controller`
- **Stub warnings**: `http3_server.hpp` and `webtransport.hpp` classes marked as non-production stubs in doc comments
- **Type deduplication**: `server_request`/`server_response` eliminated in favor of reusing `http3_request.hpp` types

## Known Limitations

- **Interop validation pending**: Phase 4 (testing against ngtcp2, quiche, MsQuic) not yet complete
- **No production-ready claim**: Library remains non-production until interop passes and human security review is complete
- AEAD support requires explicit `FLOWQ_ENABLE_OPENSSL_CRYPTO` CMake flag; default build uses plaintext/test-only protector

---

# v1.0.0 - Complete QUIC-like Protocol Library

## 🎉 Overview

FlowQ v1.0.0 was the initial release of the C++20 QUIC-like protocol library with 427 passing tests, comprehensive documentation, and examples. See v1.1.0 above for the latest changes including AEAD support and production hardening.

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

## 📊 Test Coverage (v1.0.0 baseline)

| Module | Tests | Status |
|--------|-------|--------|
| Protocol Core | 427 | ✅ Passing |
| RFC Compliance | 15 | ✅ Passing |
| Performance | 12 | ✅ Passing |
| Integration | 18 | ✅ Passing |

*Note: v1.1.0 brings the total to 495 tests with AEAD, QPACK dynamic table, and fuzz target additions.*

## 🙏 Acknowledgments

Built with:
- [Catch2](https://github.com/catchorg/Catch2) - Testing framework
- [Asio](https://github.com/chriskohlhoff/asio) - Networking library
- [vcpkg](https://github.com/microsoft/vcpkg) - Package manager

## 📄 License

See [LICENSE](LICENSE) for details.
