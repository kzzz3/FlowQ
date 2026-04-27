# M36 Endpoint Lifecycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a production-shaped UDP endpoint lifecycle and harden public construction APIs.

**Architecture:** The endpoint driver owns routing and lifecycle decisions around caller-owned ASIO sockets. It must not let test-only plaintext sessions enter production construction paths.

**Tech Stack:** C++20, Asio, Catch2 integration tests, existing `udp_session.hpp` and `session.hpp`.

---

## Files

- Create: `include/flowq/quic/endpoint_driver.hpp`
- Create: `tests/integration/quic_endpoint_driver_tests.cpp`
- Modify: `include/flowq/quic/udp_session.hpp`
- Modify: `include/flowq/quic/session.hpp`
- Modify: `tests/integration/quic_udp_session_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/development.md`

## Tasks

- [ ] Write RED tests for caller-owned socket lifecycle and explicit stop behavior.
- [ ] Write RED tests for datagram receive routing by connection ID through M35 routing helpers.
- [ ] Write RED tests for bounded send queue behavior and ASIO error reporting.
- [ ] Write RED tests proving test-only plaintext sessions cannot be built through production endpoint builders.
- [ ] Verify RED.
- [ ] Implement endpoint driver interfaces, stop handling, bounded queues, and production construction checks.
- [ ] Verify GREEN, full CTest, install, and package-consumer.
- [ ] Request Oracle review for public API wording.

## Acceptance Gate

Endpoint lifecycle is production-shaped and fail-closed, but production operation still depends on crypto/TLS/interop evidence.
