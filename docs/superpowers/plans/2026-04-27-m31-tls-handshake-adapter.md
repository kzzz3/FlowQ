# M31 TLS Handshake Adapter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Define FlowQ's TLS handshake adapter boundary and CRYPTO byte pump without implementing TLS internals.

**Architecture:** FlowQ moves CRYPTO frame bytes and observes handshake/key state. The external TLS provider owns handshake transcript, certificates, key schedule, and validation.

**Next dependency:** M31b implements a real default-off external TLS provider adapter behind this boundary. M31 alone is not enough for interop or production-candidate evidence.

**Tech Stack:** C++20 interfaces, Catch2 deterministic fake adapter, existing CRYPTO frame support, connection/session APIs.

---

## Files

- Create: `include/flowq/quic/tls_handshake.hpp`
- Create: `tests/unit/quic_tls_handshake_tests.cpp`
- Modify: `include/flowq/quic/frame.hpp`
- Modify: `include/flowq/quic/connection.hpp`
- Modify: `include/flowq/quic/session.hpp`
- Modify: `tests/unit/quic_frame_tests.cpp`
- Modify: `tests/unit/quic_connection_tests.cpp`
- Modify: `tests/unit/quic_session_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/development.md`

## Tasks

- [ ] Write RED tests for CRYPTO byte buffering by Initial, Handshake, and Application encryption levels.
- [ ] Write RED tests for adapter states: `idle`, `handshaking`, `handshake_confirmed`, and `failed`.
- [ ] Write RED tests proving Application data cannot be sent under production-required policy before handshake confirmation and key availability.
- [ ] Verify RED.
- [ ] Implement `tls_handshake_adapter` interface, `handshake_state`, `crypto_bytes`, and deterministic fake adapter helpers.
- [ ] Route CRYPTO bytes between connection/session and adapter without parsing TLS.
- [ ] Verify GREEN with focused handshake/session tests.
- [ ] Run full build, CTest, install, package-consumer gate.
- [ ] Request Oracle review before key lifecycle work.

## Acceptance Gate

FlowQ can model TLS byte flow and handshake state. TLS 1.3, certificate validation, and key schedule remain external.
