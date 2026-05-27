# M11 Connection Stream Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Connect the existing pure connection loop to the existing stream receive/send sets without adding real Application Data, TLS, short headers, or public socket APIs.

**Architecture:** Extend `connection_loop` with structural stream-state ownership and value-returning helpers that consume decoded `STREAM` / `MAX_STREAM_DATA` frames and schedule outbound stream frames through `stream_send_set::pop_frames()`. Keep packet assembly on the existing Initial/Handshake long-header test path or a clearly structural queue; do not claim wire-correct 1-RTT behavior.

**Tech Stack:** C++20, `include/flowq/quic/connection.hpp`, `include/flowq/quic/stream.hpp`, Catch2, CMake preset `windows-msvc-vcpkg`.

---

## File Structure

- Modify `include/flowq/quic/connection.hpp`: add stream receive/send sets and small helpers for inbound stream frame routing and outbound stream frame scheduling.
- Modify `tests/unit/quic_connection_tests.cpp`: add deterministic connection-stream routing tests.
- Modify `docs/development.md`: document M11 as structural connection/stream integration only.

## Task 1: Route Inbound STREAM Frames into Receive Streams

**Files:**
- Modify: `tests/unit/quic_connection_tests.cpp`
- Modify: `include/flowq/quic/connection.hpp`

- [ ] **Step 1: Write failing tests**

Add tests showing a parsed packet event can also expose stream deliveries. Use existing plaintext/test packet helpers from `quic_connection_tests.cpp` and a queued inbound packet containing `stream_frame{0, 0, false, true, false, text("hello")}`.

Expected assertions:

```cpp
REQUIRE(result.actions.size() >= 1);
REQUIRE(result.stream_deliveries.size() == 1);
CHECK(result.stream_deliveries[0].stream_id == 0);
CHECK(as_string(result.stream_deliveries[0].result.data) == "hello");
CHECK(result.stream_deliveries[0].result.ok());
```

- [ ] **Step 2: Run test to verify RED**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg
```

Expected: build fails because the connection receive result does not expose `stream_deliveries` or equivalent routing yet.

- [ ] **Step 3: Implement minimal routing**

Add a result vector on the connection receive path, route decoded `stream_frame` variants into a `stream_receive_set`, and append the resulting `stream_delivery`. Preserve existing packet event and ACK behavior.

- [ ] **Step 4: Run focused tests**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 -R "connection loop" }
```

Expected: connection tests pass.

## Task 2: Schedule Outbound Stream Frames from Connection State

**Files:**
- Modify: `tests/unit/quic_connection_tests.cpp`
- Modify: `include/flowq/quic/connection.hpp`

- [ ] **Step 1: Write failing tests**

Add a test that appends bytes to connection-owned stream send state, schedules stream IDs `{0}`, and observes a queued structural frame vector containing one `stream_frame`.

Expected assertions:

```cpp
REQUIRE(loop.append_stream_data(0, text("hello")).ok());
auto scheduled = loop.schedule_stream_frames(std::vector<std::uint64_t>{0}, 4, 16);
REQUIRE(scheduled.ok());
REQUIRE(scheduled.frames.size() == 1);
REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(scheduled.frames[0]));
```

- [ ] **Step 2: Run test to verify RED**

Expected: build fails because `append_stream_data()` and `schedule_stream_frames()` do not exist.

- [ ] **Step 3: Implement minimal connection send wrappers**

Add wrappers that delegate to `stream_send_set::append()` and `stream_send_set::pop_frames()`. Keep them synchronous and value-oriented.

- [ ] **Step 4: Run focused tests**

Expected: connection tests pass.

## Task 3: Documentation and Verification

**Files:**
- Modify: `docs/development.md`

- [ ] **Step 1: Update docs**

Add an M11 section explaining that connection state can route STREAM frames into stream sets and schedule outbound stream frames structurally, while still excluding short headers, real Application Data, TLS, public APIs, and congestion.

- [ ] **Step 2: Full verification**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 }
```

Expected: build succeeds and all tests pass.

## Self-Review

- Spec coverage: Routes inbound STREAM frames, exposes stream deliveries, delegates outbound scheduling, documents non-goals.
- Placeholder scan: No deferred item is a hidden implementation placeholder; deferred features are listed as scope boundaries.
- Type consistency: Uses existing `stream_receive_set`, `stream_send_set`, `stream_delivery`, `stream_frame_schedule_result`, and `frame` names.
