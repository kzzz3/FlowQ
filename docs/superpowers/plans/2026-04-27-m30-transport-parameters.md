# M30 Transport Parameters Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add structural QUIC transport parameter encode/decode and map selected parameters into FlowQ session/connection config.

**Architecture:** Implement a deterministic codec independent of TLS. TLS extension binding and authenticated negotiation remain M31 scope.

**Tech Stack:** C++20, Catch2, existing QUIC varint helpers, session/connection config values.

---

## Files

- Create: `include/flowq/quic/transport_parameters.hpp`
- Create: `tests/unit/quic_transport_parameters_tests.cpp`
- Modify: `include/flowq/quic/session.hpp`
- Modify: `include/flowq/quic/connection.hpp`
- Modify: `tests/unit/quic_session_tests.cpp`
- Modify: `tests/unit/quic_connection_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/development.md`
- Modify: `PLAN.md`

## Tasks

- [ ] Write RED round-trip tests for `initial_max_data`, `initial_max_stream_data_bidi_local`, `initial_max_stream_data_bidi_remote`, `initial_max_stream_data_uni`, `max_idle_timeout`, `max_udp_payload_size`, `active_connection_id_limit`, and `disable_active_migration`.
- [ ] Write RED malformed-input tests for duplicate parameters, truncated varints, invalid values, and preserved unknown parameters.
- [ ] Verify RED.
- [ ] Implement `transport_parameter`, `transport_parameters`, encode/decode result structs, and codec helpers.
- [ ] Map decoded flow-control parameters into `connection_loop_config` and `session_config` without TLS binding.
- [ ] Verify GREEN with `ctest --preset windows-msvc-vcpkg --timeout 10 -R "transport parameter|session|connection"`.
- [ ] Run full build, CTest, install, package-consumer gate.
- [ ] Document that transport parameters are structural until TLS extension binding exists.

## Acceptance Gate

Transport parameters round-trip deterministically, malformed inputs return `protocol_error`, and production negotiation is not claimed.
