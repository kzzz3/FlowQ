# M17 Minimal Close and Stream Reset Codec Design

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development for implementation. This stage adds reset/stop frame values and minimal stream effects; it does not implement a full public stream API.

**Goal:** Support structural `RESET_STREAM` and `STOP_SENDING` frame codecs plus minimal stream/connection routing effects.

**Architecture:** M17 extends `frame.hpp` first, then routes decoded control frames into stream state. Application error code policy remains inert and test-visible.

**Tech Stack:** C++20, `frame.hpp`, `stream.hpp`, `connection.hpp`, Catch2.

---

## Scope

- Add `reset_stream_frame` codec support.
- Add `stop_sending_frame` codec support.
- Add minimal receive/send stream state markers for reset or stop.
- Route reset/stop frames through connection-owned stream state.

## Behavioral Rules

- `RESET_STREAM` carries stream ID, application error code, and final size.
- `STOP_SENDING` carries stream ID and application error code.
- Reset/stop state is explicit and observable in tests.
- Data beyond an established final size remains invalid.

## Non-Goals

- Full stream state machine.
- Application callback policy.
- Public async stream cancellation API.
- Complex connection close/error policy.

## Verification

Run frame, stream, connection tests, then full CTest.
