# FlowQ Production Push Plan

**Created**: 2026-05-29
**Purpose**: Push FlowQ from "feature-complete non-production baseline" to "production-grade QUIC library"
**Status**: Active

---

## Executive Summary

FlowQ has completed all milestones M20-M39 and passed all optimization phases. The codebase is clean (0 TODO/FIXME markers, consistent error handling, 484 tests passing). However, significant gaps remain before production-grade quality:

1. **No real AEAD** — plaintext_packet_protector is the only implementation
2. **Stub implementations** — webtransport, http3_server, interop_runner are placeholders
3. **API surface issues** — 14 undocumented raw pointers, detail namespace leakage
4. **Protocol gaps** — QPACK bugs, simplified congestion, incomplete header protection

---

## Phase 0: Codebase Cleanup (No external dependencies)

### Tasks

- [ ] **T0.1**: Remove dead `#include <stdexcept>` from `connection.hpp` line 9
- [ ] **T0.2**: Replace `UINT64_MAX` with `std::numeric_limits<std::uint64_t>::max()` in 11 locations (stream.hpp, congestion.hpp, congestion_algorithms.hpp)
- [ ] **T0.3**: Add `[[nodiscard]]` to `qpack.hpp:415` `decoder::decode()`
- [ ] **T0.4**: Add `[[nodiscard]]` to `http3_request.hpp:241,290` `request_decoder::decode()` and `response_decoder::decode()`
- [ ] **T0.5**: Fix http3.hpp silent error swallowing — change `encode_data_frame`, `encode_goaway_frame`, `encode_settings_frame` to return result types
- [ ] **T0.6**: Add ownership/lifetime documentation to all 14 raw pointer members (`@pre` comments)
- [ ] **T0.7**: Add thread-safety documentation to `session`, `connection_loop`, `endpoint_driver`, `diagnostics`, `congestion_controller`
- [ ] **T0.8**: Mark `http3_server.hpp` and `webtransport.hpp` classes as non-production stubs in doc comments
- [ ] **T0.9**: Eliminate `server_request`/`server_response` duplication by reusing `http3_request.hpp` types

### Success Criteria
- All `[[nodiscard]]` on value-returning methods
- All raw pointers documented with lifetime requirements
- No silent error swallowing
- Thread-safety contracts documented
- Build passes, 484 tests pass

---

## Phase 1: API Surface Hardening

### Tasks

- [ ] **T1.1**: Move `detail::` namespaces to internal headers or add `FLOWQ_DETAIL` macro guard
- [ ] **T1.2**: Gate `connection_loop` inspection methods (`sent_packets()`, `congestion()`, `receive_stream()`, `send_stream()`) behind `FLOWQ_ENABLE_INSPECTION` build option
- [ ] **T1.3**: Consolidate `session_config` and `connection_loop_config` — make session_config contain connection_loop_config
- [ ] **T1.4**: Consolidate `apply_transport_parameters` overloads
- [ ] **T1.5**: Remove dead `error_code::stream_reset` or document as reserved
- [ ] **T1.6**: Remove dead `error_code::cancelled` or document as reserved for Asio layer

### Success Criteria
- `detail::` names not accessible to external consumers
- Inspection methods gated behind build option
- No duplicated config structs
- Error taxonomy is clean and documented

---

## Phase 2: QPACK Fixes

### Tasks

- [ ] **T2.1**: Fix QPACK delta-base encoding (currently hardcoded to 0)
- [ ] **T2.2**: Fix QPACK multi-byte length decoding (currently only single-byte)
- [ ] **T2.3**: Add QPACK dynamic table support (currently static-only)
- [ ] **T2.4**: Add QPACK test vectors from RFC 9204

### Success Criteria
- QPACK encoder/decoder passes RFC 9204 test vectors
- Dynamic table works correctly
- All QPACK tests pass

---

## Phase 3: Real AEAD Packet Protection (Requires Oracle guidance)

### Tasks

- [ ] **T3.1**: Implement `openssl_aead_protector` class implementing `packet_protector` interface
  - AES-128-GCM support
  - ChaCha20-Poly1305 support
  - Proper key/IV/HP key derivation from TLS secrets
- [ ] **T3.2**: Implement header protection (XOR mask per RFC 9001 §5.4)
- [ ] **T3.3**: Implement key update mechanism (Phase 3 of key_lifecycle)
- [ ] **T3.4**: Validate against RFC 9001 Appendix A test vectors
- [ ] **T3.5**: Update packet_pipeline.hpp to use AEAD protector when available
- [ ] **T3.6**: Add AEAD integration tests
- [ ] **T3.7**: Add AEAD performance benchmarks

### Success Criteria
- `openssl_aead_protector` passes RFC 9001 test vectors
- Header protection works correctly
- Key updates work
- All existing tests still pass
- New AEAD tests pass

---

## Phase 4: Interop Validation

### Tasks

- [ ] **T4.1**: Implement real interop runner (replace stub in `interop_runner.hpp`)
- [ ] **T4.2**: Test against ngtcp2
- [ ] **T4.3**: Test against quiche
- [ ] **T4.4**: Test against MsQuic
- [ ] **T4.5**: Record interop results in `docs/interop/results.md`
- [ ] **T4.6**: Fix any interop issues found

### Success Criteria
- Basic handshake passes against 2+ QUIC implementations
- Stream echo passes against 2+ implementations
- Loss recovery passes against 2+ implementations
- Interop results documented with peer versions

---

## Phase 5: Production Hardening

### Tasks

- [ ] **T5.1**: Add comprehensive fuzz targets for all parsers
- [ ] **T5.2**: Add AddressSanitizer + UndefinedBehaviorSanitizer CI gates
- [ ] **T5.3**: Add ThreadSanitizer for concurrent access detection
- [ ] **T5.4**: Add memory leak detection
- [ ] **T5.5**: Performance benchmarking against baseline
- [ ] **T5.6**: Security review preparation (document threat model)

### Success Criteria
- Fuzz targets run without crashes for 1M+ iterations
- Sanitizers pass clean
- Performance meets or exceeds baseline
- Threat model documented

---

## Phase 6: Documentation & Release

### Tasks

- [ ] **T6.1**: Update README.md with actual production status
- [ ] **T6.2**: Update RELEASE_NOTES.md with real capabilities
- [ ] **T6.3**: Create migration guide from v1.0.0
- [ ] **T6.4**: Update all docs to reflect AEAD support
- [ ] **T6.5**: Tag v2.0.0 release

### Success Criteria
- Documentation accurately reflects production capabilities
- Migration guide exists
- v2.0.0 tagged and released

---

## Execution Order

```
Phase 0 (Cleanup) — immediate, no dependencies
    ↓
Phase 1 (API Hardening) — depends on Phase 0
    ↓
Phase 2 (QPACK) — independent, can parallel with Phase 1
    ↓
Phase 3 (AEAD) — depends on Oracle guidance, hardest part
    ↓
Phase 4 (Interop) — depends on Phase 3
    ↓
Phase 5 (Hardening) — can start during Phase 3
    ↓
Phase 6 (Release) — depends on all prior phases
```

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| AEAD implementation is complex | Get Oracle guidance, implement incrementally, validate against test vectors |
| Interop testing requires external deps | Make optional, document setup, use CI for automated testing |
| Breaking existing tests | Run full test suite after every change, use facade pattern |
| Performance regression | Benchmark before/after, profile hot paths |
