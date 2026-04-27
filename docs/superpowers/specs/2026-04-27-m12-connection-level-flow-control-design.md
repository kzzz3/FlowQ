# M12 Connection-Level Flow Control Design

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development for implementation. This stage adds aggregate connection byte-credit accounting only; do not add stream-count limits, congestion, or public API behavior.

**Goal:** Bound outbound STREAM data by connection-level credit and expose structural `MAX_DATA` / `DATA_BLOCKED` behavior.

**Architecture:** M12 adds a small value-oriented connection flow-control state near `connection_loop`. It composes with stream-level credit rather than replacing M7/M9 stream behavior.

**Tech Stack:** C++20, existing M8 flow-control frame structs, `connection.hpp`, `stream.hpp`, Catch2.

---

## Scope

- Track peer connection send credit as an absolute maximum data offset.
- Track total stream data scheduled/sent at connection level.
- Apply `max_data_frame` monotonically.
- Emit one query-only `data_blocked_frame` when aggregate credit prevents stream progress.
- Preserve per-stream `MAX_STREAM_DATA` / `STREAM_DATA_BLOCKED` semantics from M9/M10.

## Behavioral Rules

- Connection credit is absolute, not incremental.
- STREAM data must satisfy both stream credit and connection credit.
- Control frames do not consume stream data credit.
- Repeated `DATA_BLOCKED` emission is allowed unless a later milestone adds deduplication.

## Non-Goals

- Dynamic receive-window policy.
- Stream-count limits and `MAX_STREAMS` / `STREAMS_BLOCKED`.
- Congestion control, pacing, real packet scheduling, public APIs.

## Verification

Run focused connection flow-control tests, then full CTest.
