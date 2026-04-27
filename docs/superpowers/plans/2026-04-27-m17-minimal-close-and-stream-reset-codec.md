# M17 Minimal Close and Stream Reset Codec Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add structural `RESET_STREAM` and `STOP_SENDING` frame support plus minimal stream-state effects for loopback tests.

**Architecture:** Extend the frame codec with RFC 9000 reset/stop frame value types, then add pure stream/connection handling that marks send or receive sides stopped without introducing a public async stream API.

**Tech Stack:** C++20, `frame.hpp`, `stream.hpp`, `connection.hpp`, Catch2, CMake preset `windows-msvc-vcpkg`.

---

## File Structure

- Modify `include/flowq/quic/frame.hpp`: add `reset_stream_frame` and `stop_sending_frame` structural codec support.
- Modify `include/flowq/quic/stream.hpp`: add minimal reset/stop state queries and handlers.
- Modify `include/flowq/quic/connection.hpp`: route reset/stop frames to stream sets and surface events.
- Modify `tests/unit/quic_frame_tests.cpp`: add frame codec tests.
- Modify `tests/unit/quic_stream_tests.cpp`: add stream state tests.
- Modify `tests/unit/quic_connection_tests.cpp`: add routing tests.
- Modify `docs/development.md`: document M17 scope.

## Task 1: Codec RESET_STREAM and STOP_SENDING

**Files:**
- Modify: `tests/unit/quic_frame_tests.cpp`
- Modify: `include/flowq/quic/frame.hpp`

- [x] **Step 1: Write failing tests**

Round-trip `reset_stream_frame{stream_id, application_error_code, final_size}` and `stop_sending_frame{stream_id, application_error_code}` through the existing frame encoder/decoder.

Expected assertions:

```cpp
REQUIRE(std::holds_alternative<flowq::quic::reset_stream_frame>(decoded.frames[0]));
CHECK(std::get<flowq::quic::reset_stream_frame>(decoded.frames[0]).final_size == 12);
```

- [x] **Step 2: Run test to verify RED**

Expected: reset/stop frame types are unsupported.

- [x] **Step 3: Implement structural codecs**

Add frame structs, variant alternatives, encode branches, decode branches, and malformed/truncated tests matching existing codec style.

- [x] **Step 4: Run focused tests**

Expected: frame codec tests pass.

## Task 2: Minimal Stream and Connection Effects

**Files:**
- Modify: `tests/unit/quic_stream_tests.cpp`
- Modify: `tests/unit/quic_connection_tests.cpp`
- Modify: `include/flowq/quic/stream.hpp`
- Modify: `include/flowq/quic/connection.hpp`

- [x] **Step 1: Write failing tests**

For `RESET_STREAM`, assert receive state records a final size and stops delivering further bytes beyond that size. For `STOP_SENDING`, assert send state reports stopped and no longer emits new stream data except already-accounted reset signaling if added.

- [x] **Step 2: Run test to verify RED**

Expected: stream reset/stop state APIs are absent.

- [x] **Step 3: Implement minimal state handlers**

Add value-only handlers and queries. Keep application error-code policy inert; store and expose values for tests.

- [x] **Step 4: Run focused tests**

Expected: stream and connection reset/stop tests pass.

## Task 3: Documentation and Verification

**Files:**
- Modify: `docs/development.md`

- [x] **Step 1: Update docs**

Document structural reset/stop support and explicitly exclude complete stream state machines, app callback policy, and production interop.

- [x] **Step 2: Full verification**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 }
```

Expected: build succeeds and all tests pass.

## Self-Review

- Spec coverage: Adds reset/stop codecs, minimal stream effects, connection routing, explicit non-goals, and docs.
- Placeholder scan: Full lifecycle/application policy is explicitly deferred.
- Type consistency: Uses frame naming consistent with existing `*_frame` structs.
