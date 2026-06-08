# Changelog

All notable changes to FlowQ are documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/).

## [1.1.0] - 2026-06-08

### Added
- Zero-copy packet builder integrated into packet_pipeline
- `protect_in_place()` for in-buffer AEAD encryption
- `datagram_buffer_pool` for buffer reuse
- CUBIC congestion control (RFC 8312) with TCP friendliness and fast convergence
- Pacing-cwnd synchronization on all congestion window changes
- 7 new CUBIC test cases
- Production readiness assessment (`docs/production/production-readiness-assessment.md`)

### Changed
- Default congestion algorithm: NewReno to CUBIC
- Pacing `min_interval_`: 100us to 50us
- Zero-copy AAD/plaintext now uses span instead of vector copy
- `max_datagram_size` from config replaces hardcoded 1200

### Fixed
- Pacing-cwnd synchronization on congestion window changes
- CUBIC `last_max_cwnd` initialization
- CUBIC recovery to congestion_avoidance state transition
- release_readiness bash script execution permissions
- checklist validator false positives for API/technical terms

## [1.0.0] - 2026-06-01

### Added
- OpenSSL 3.5+ QUIC TLS integration
- Multi-cipher AEAD: AES-128-GCM, AES-256-GCM, ChaCha20-Poly1305
- Cipher-suite-aware header protection
- AEAD key rotation (RFC 9000 Section 6)
- Secure key material zeroing
- BBR congestion control
- Pacing controller (RFC 9002 Section 7.7)
- aioquic 1.3.0 interop (handshake, stream echo, loss recovery)
- ngtcp2 1.20.0 interop (initial packet generation)
- Fuzz targets: packet_header, frame_decode, qpack
- ASan/UBSan CI workflow
- Benchmark framework (40 scenarios)
- Soak test (10,000 connections)

## [0.1.0] - 2026-05-29

### Added
- QUIC v1 transport core
- Varint, packet number, frame, header, transport parameter codecs
- Packet pipeline with assembly/parsing
- Connection loop with packet-space management
- Stream state with flow control
- ACK/loss detection, RTT estimation, PTO
- NewReno congestion control
- Connection ID routing
- Stateless reset detection and generation
- Endpoint driver lifecycle
- CMake package export
