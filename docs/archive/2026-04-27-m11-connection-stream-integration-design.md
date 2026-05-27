# M11 Connection Stream Integration Design

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development for implementation. This stage integrates existing stream cores into the structural connection loop; it must not add real Application Data, short headers, TLS, public APIs, sockets, or production interop claims.

**Goal:** Let `connection_loop` route inbound STREAM frames to receive streams and expose outbound stream scheduling through connection-owned send state.

**Architecture:** M11 adds connection-level ownership/wrappers around `stream_receive_set` and `stream_send_set`. It preserves the current pure connection model and uses existing frame values rather than packet-security features.

**Tech Stack:** C++20, `connection.hpp`, `stream.hpp`, existing frame/packet pipeline, Catch2.

---

## Scope

- Add connection-owned stream receive/send state.
- Route decoded inbound `stream_frame` values into `stream_receive_set`.
- Surface stream deliveries as value results from inbound packet processing.
- Add wrappers for appending outbound stream bytes and calling `stream_send_set::pop_frames()`.
- Keep Initial/Handshake structural packet tests unchanged unless a test-only structural path is explicitly used.

## Behavioral Rules

- Existing packet parse and ACK behavior remain intact.
- STREAM delivery errors become explicit error values or close actions following current connection style.
- Outbound stream scheduling delegates to M10 and preserves caller-provided stream order.
- No new priority policy is introduced.

## Non-Goals

- Application packet space, short headers, real TLS/AEAD/header protection.
- Public `flowq::connection` or `flowq::stream` async APIs.
- Congestion, pacing, connection-level flow control, reset/stop handling.

## Verification

Run build and full CTest after implementation. LSP diagnostics may be attempted if `clangd` is installed.
