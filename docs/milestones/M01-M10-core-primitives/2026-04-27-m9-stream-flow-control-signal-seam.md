# M9 Stream Flow-Control Signal Seam Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a pure stream-scope seam between M8 flow-control frame values and M7 stream send credit/blocking state.

**Architecture:** Extend `include/flowq/quic/stream.hpp` with small frame-value helpers on existing stream send state/set types. Add Catch2 coverage in `tests/unit/quic_stream_tests.cpp` and document the M9 boundary in `docs/development.md`.

**Tech Stack:** C++20, FlowQ QUIC stream/frame headers, Catch2, CMake preset `windows-msvc-vcpkg`.

---

## File Structure

- Modify `include/flowq/quic/stream.hpp`: add `max_stream_data_frame` overloads and `blocked_frame()` query helpers.
- Modify `tests/unit/quic_stream_tests.cpp`: add M9 stream signal tests.
- Modify `docs/development.md`: add M9 signal seam scope.

## Task 1: Apply MAX_STREAM_DATA Frame Values

**Files:**
- Modify: `tests/unit/quic_stream_tests.cpp`
- Modify: `include/flowq/quic/stream.hpp`

- [ ] **Step 1: Write failing tests**

Add tests for single-state and set-level `MAX_STREAM_DATA` application:

```cpp
TEST_CASE("stream send state applies MAX_STREAM_DATA frame credit") {
    flowq::quic::stream_send_state state{0, 2};
    REQUIRE(state.append(text("hello")).ok());
    REQUIRE(state.pop_frame(16).has_frame);

    state.update_max_data(flowq::quic::max_stream_data_frame{0, 5});
    auto suffix = state.pop_frame(16);

    REQUIRE(suffix.ok());
    REQUIRE(suffix.has_frame);
    CHECK(suffix.range.offset == 2);
    CHECK(as_string(suffix.frame.data) == "llo");
}

TEST_CASE("stream send state ignores stale and mismatched MAX_STREAM_DATA frames") {
    flowq::quic::stream_send_state state{0, 2};
    REQUIRE(state.append(text("hello")).ok());
    REQUIRE(state.pop_frame(16).has_frame);

    state.update_max_data(flowq::quic::max_stream_data_frame{4, 5});
    CHECK(state.blocked());
    state.update_max_data(flowq::quic::max_stream_data_frame{0, 5});
    state.update_max_data(flowq::quic::max_stream_data_frame{0, 2});
    auto suffix = state.pop_frame(16);

    REQUIRE(suffix.ok());
    REQUIRE(suffix.has_frame);
    CHECK(as_string(suffix.frame.data) == "llo");
}
```

- [ ] **Step 2: Run tests to verify RED**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 -R "MAX_STREAM_DATA frame" }
```

Expected: build fails because `stream_send_state::update_max_data(const max_stream_data_frame&)` does not exist.

- [ ] **Step 3: Implement minimal overloads**

Add single-state and set-level overloads. Single-state overload ignores frames whose `stream_id` differs from the state stream ID. Set-level overload routes by `frame.stream_id`.

- [ ] **Step 4: Run tests to verify GREEN**

Run the selected tests. Expected: PASS.

## Task 2: Report STREAM_DATA_BLOCKED Frame Values

**Files:**
- Modify: `tests/unit/quic_stream_tests.cpp`
- Modify: `include/flowq/quic/stream.hpp`

- [ ] **Step 1: Write failing tests**

Add tests that a blocked stream reports `stream_data_blocked_frame` and non-blocked cases return no frame.

- [ ] **Step 2: Run tests to verify RED**

Expected: build fails because `blocked_frame()` does not exist.

- [ ] **Step 3: Implement `blocked_frame()` helpers**

Add `stream_send_state::blocked_frame() const noexcept` and `stream_send_set::blocked_frame(stream_id) const noexcept`.

- [ ] **Step 4: Run tests to verify GREEN**

Run selected blocked-frame tests. Expected: PASS.

## Task 3: Documentation and Full Verification

**Files:**
- Modify: `docs/development.md`

- [ ] **Step 1: Update docs**

Add an M9 section after M8 explaining the pure stream-scope signal seam and explicit non-goals.

- [ ] **Step 2: Full verification**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 }
```

Expected: build succeeds and all tests pass.

## Self-Review

- Spec coverage: Tests and tasks cover frame credit application, stale/mismatch behavior, blocked frame reporting, docs, and verification.
- Placeholder scan: No TBD/TODO placeholders are present.
- Type consistency: Methods and frame value types match the M9 design.
