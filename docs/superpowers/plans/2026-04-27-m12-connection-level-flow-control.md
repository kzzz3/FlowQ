# M12 Connection-Level Flow Control Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add pure connection-level send/receive byte-credit accounting around the existing stream-level flow-control seam.

**Architecture:** Introduce a small connection flow-control state that tracks peer connection send credit, local receive credit, bytes scheduled across streams, and structural `MAX_DATA` / `DATA_BLOCKED` signals. Integrate it with M11 connection stream scheduling without changing frame codecs.

**Tech Stack:** C++20, existing flow-control frame structs in `frame.hpp`, `connection.hpp`, `stream.hpp`, Catch2, CMake preset `windows-msvc-vcpkg`.

---

## File Structure

- Modify `include/flowq/quic/connection.hpp`: add connection-level flow-control state and wrappers.
- Modify `include/flowq/quic/stream.hpp`: add scheduler hooks only if needed to cap aggregate bytes without duplicating stream logic.
- Modify `tests/unit/quic_connection_tests.cpp`: add connection-level credit tests.
- Modify `docs/development.md`: document M12 scope and non-goals.

## Task 1: Apply MAX_DATA to Connection Send Credit

**Files:**
- Modify: `tests/unit/quic_connection_tests.cpp`
- Modify: `include/flowq/quic/connection.hpp`

- [ ] **Step 1: Write failing tests**

Create a connection with initial connection send credit `2`, append `"hello"` to stream `0`, schedule stream frames, then apply `max_data_frame{5}` and schedule the suffix.

Expected assertions:

```cpp
auto first = loop.schedule_stream_frames(std::vector<std::uint64_t>{0}, 4, 16);
REQUIRE(first.ok());
CHECK(as_string(std::get<flowq::quic::stream_frame>(first.frames[0]).data) == "he");
loop.update_max_data(flowq::quic::max_data_frame{5});
auto second = loop.schedule_stream_frames(std::vector<std::uint64_t>{0}, 4, 16);
REQUIRE(second.ok());
CHECK(as_string(std::get<flowq::quic::stream_frame>(second.frames[0]).data) == "llo");
```

- [ ] **Step 2: Run test to verify RED**

Expected: build fails because connection-level send credit configuration/update is absent.

- [ ] **Step 3: Implement minimal send-credit accounting**

Track total stream bytes scheduled as connection data. Cap scheduled STREAM bytes by remaining peer connection credit. Apply `max_data_frame.maximum_data` monotonically.

- [ ] **Step 4: Run focused tests**

Expected: connection flow-control tests pass.

## Task 2: Emit DATA_BLOCKED When Aggregate Credit Blocks Progress

**Files:**
- Modify: `tests/unit/quic_connection_tests.cpp`
- Modify: `include/flowq/quic/connection.hpp`

- [ ] **Step 1: Write failing tests**

Append data on two streams with exhausted connection credit and verify the scheduler emits one `data_blocked_frame` with the current absolute maximum.

Expected assertions:

```cpp
REQUIRE(std::holds_alternative<flowq::quic::data_blocked_frame>(blocked.frames[0]));
CHECK(std::get<flowq::quic::data_blocked_frame>(blocked.frames[0]).maximum_data == 2);
```

- [ ] **Step 2: Run test to verify RED**

Expected: scheduler currently has no connection-level blocked output.

- [ ] **Step 3: Implement `DATA_BLOCKED` query/output**

Add a query-only blocked signal that emits at most one `DATA_BLOCKED` per scheduling call when aggregate credit prevents any stream data from being scheduled.

- [ ] **Step 4: Run focused tests**

Expected: blocked tests pass.

## Task 3: Documentation and Verification

**Files:**
- Modify: `docs/development.md`

- [ ] **Step 1: Update docs**

Document that M12 covers connection-level byte credit and `MAX_DATA` / `DATA_BLOCKED` values only. Explicitly exclude stream-count limits, full receive-window policy, congestion, and public APIs.

- [ ] **Step 2: Full verification**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 }
```

Expected: build succeeds and all tests pass.

## Self-Review

- Spec coverage: Applies `MAX_DATA`, caps aggregate stream bytes, emits `DATA_BLOCKED`, documents boundaries.
- Placeholder scan: No hidden placeholders; stream-count and production policies are explicit non-goals.
- Type consistency: Uses existing `max_data_frame`, `data_blocked_frame`, and scheduler result naming.
