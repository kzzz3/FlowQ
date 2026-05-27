# M33 Key Lifecycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add deterministic key availability, installation, discard, and packet-space lifecycle gates.

**Architecture:** FlowQ stores key lifecycle state and gates packet spaces; external TLS/crypto supplies real key material through earlier boundaries.

**Tech Stack:** C++20 value types, Catch2, connection/session integration.

---

## Files

- Create: `include/flowq/quic/key_lifecycle.hpp`
- Create: `tests/unit/quic_key_lifecycle_tests.cpp`
- Modify: `include/flowq/quic/connection.hpp`
- Modify: `include/flowq/quic/session.hpp`
- Modify: `tests/unit/quic_connection_tests.cpp`
- Modify: `tests/unit/quic_session_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/development.md`

## Tasks

- [x] Write RED tests for installing Initial, Handshake, 0-RTT, and 1-RTT send/receive availability as value events.
- [x] Write RED tests for discarding Initial keys after Handshake keys are available.
- [x] Write RED tests for discarding Handshake keys after handshake confirmation.
- [x] Write RED tests proving ACK/loss state tied to discarded packet spaces is ignored or removed safely.
- [x] Verify RED.
- [x] Implement `key_lifecycle_state`, `encryption_level`, key availability events, and discard decisions.
- [x] Integrate lifecycle gates into connection/session paths.
- [x] Verify GREEN, full CTest, install, and package-consumer.
- [ ] Request Oracle review.

## Acceptance Gate

Packet-space lifecycle is deterministic and safe. FlowQ still does not own TLS secrets or implement key schedule.
