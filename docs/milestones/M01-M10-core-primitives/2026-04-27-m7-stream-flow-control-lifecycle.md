# M7 Stream Flow-Control and Lifecycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add minimal pure stream-level flow-control accounting and FIN lifecycle inspectors to the existing FlowQ STREAM core.

**Architecture:** Extend `include/flowq/quic/stream.hpp` in place, preserving the current header-only pure-core style. Add focused Catch2 tests in `tests/unit/quic_stream_tests.cpp`, then update `docs/development.md` to document the M7 boundary and explicit deferrals.

**Tech Stack:** C++20, Catch2, CMake preset `windows-msvc-vcpkg`, existing FlowQ QUIC stream/frame/error primitives.

---

## File Structure

- Modify `include/flowq/quic/stream.hpp`: add flow-control error helper, receive/send credit state, lifecycle inspectors, and set routing helpers.
- Modify `tests/unit/quic_stream_tests.cpp`: add M7 receive credit, send credit, lifecycle, retransmission, and set routing tests.
- Modify `docs/development.md`: add M7 milestone scope after the M6 section.

## Task 1: Receive Stream Credit

**Files:**
- Modify: `tests/unit/quic_stream_tests.cpp`
- Modify: `include/flowq/quic/stream.hpp`

- [ ] **Step 1: Write failing tests**

Add tests asserting that receive credit is an offset limit, exceeding credit returns `flow_control_error`, and monotonic credit increase permits later frames:

```cpp
TEST_CASE("stream receive state enforces stream data credit") {
    flowq::quic::stream_receive_state state{5};

    auto accepted = state.receive(stream(0, 0, "hello"));
    auto blocked = state.receive(stream(0, 5, "!"));

    REQUIRE(accepted.ok());
    CHECK(as_string(accepted.data) == "hello");
    CHECK_FALSE(blocked.ok());
    CHECK(blocked.error.code() == flowq::error_code::flow_control_error);
}

TEST_CASE("stream receive state accepts data after receive credit increases") {
    flowq::quic::stream_receive_state state{2};

    REQUIRE(state.receive(stream(0, 0, "he")).ok());
    auto blocked = state.receive(stream(0, 2, "llo"));
    state.update_max_data(5);
    auto accepted = state.receive(stream(0, 2, "llo"));

    CHECK_FALSE(blocked.ok());
    CHECK(blocked.error.code() == flowq::error_code::flow_control_error);
    REQUIRE(accepted.ok());
    CHECK(as_string(accepted.data) == "llo");
    CHECK(state.next_offset() == 5);
}
```

- [ ] **Step 2: Run tests to verify RED**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 -R "stream receive state enforces stream data credit|stream receive state accepts data after receive credit increases" }
```

Expected: build fails because `stream_receive_state{5}` or `update_max_data` is not implemented.

- [ ] **Step 3: Implement receive credit**

Add `detail::stream_flow_control_error`, receive constructor, `update_max_data`, `max_data`, and check `end > max_data_` after overflow detection and before final-size/overlap mutation.

- [ ] **Step 4: Run tests to verify GREEN**

Run the same command. Expected: selected tests pass.

## Task 2: Send Stream Credit

**Files:**
- Modify: `tests/unit/quic_stream_tests.cpp`
- Modify: `include/flowq/quic/stream.hpp`

- [ ] **Step 1: Write failing tests**

Add tests for credit clipping, blocked state, and monotonic peer credit increase:

```cpp
TEST_CASE("stream send state limits new data by peer stream credit") {
    flowq::quic::stream_send_state state{0, 2};
    REQUIRE(state.append(text("hello")).ok());

    auto first = state.pop_frame(16);
    auto blocked = state.pop_frame(16);

    REQUIRE(first.ok());
    REQUIRE(first.has_frame);
    CHECK(as_string(first.frame.data) == "he");
    CHECK(first.range.offset == 0);
    CHECK(first.range.length == 2);
    REQUIRE(blocked.ok());
    CHECK_FALSE(blocked.has_frame);
    CHECK(state.blocked());
}

TEST_CASE("stream send state emits pending data after peer credit increases") {
    flowq::quic::stream_send_state state{0, 2};
    REQUIRE(state.append(text("hello")).ok());
    REQUIRE(state.pop_frame(16).has_frame);

    state.update_max_data(5);
    auto second = state.pop_frame(16);

    REQUIRE(second.ok());
    REQUIRE(second.has_frame);
    CHECK(second.range.offset == 2);
    CHECK(as_string(second.frame.data) == "llo");
    CHECK_FALSE(state.blocked());
}
```

- [ ] **Step 2: Run tests to verify RED**

Run selected send credit tests and confirm compile/test failure before implementation.

- [ ] **Step 3: Implement send credit**

Add send constructor with peer limit, monotonic `update_max_data`, `max_data`, `blocked`, and limit new unsent data by `peer_max_data_ - next_unsent_offset_`. Keep lost retransmission independent of this new-data credit.

- [ ] **Step 4: Run tests to verify GREEN**

Run selected send credit tests. Expected: PASS.

## Task 3: FIN Lifecycle and Retransmission Boundaries

**Files:**
- Modify: `tests/unit/quic_stream_tests.cpp`
- Modify: `include/flowq/quic/stream.hpp`

- [ ] **Step 1: Write failing tests**

Add tests that FIN waits for credit, lifecycle inspectors report terminal state only after FIN ACK, and lost data retransmits even when unsent data is blocked.

- [ ] **Step 2: Run tests to verify RED**

Run selected lifecycle tests. Expected: fail because inspectors and/or credit-aware FIN are missing.

- [ ] **Step 3: Implement lifecycle inspectors**

Add `finished()`, `fin_sent()`, `fin_acked()`, and `closed()` inspectors. Ensure `pop_frame` cannot emit FIN before all final-size bytes are emitted within credit.

- [ ] **Step 4: Run tests to verify GREEN**

Run selected lifecycle tests. Expected: PASS.

## Task 4: Set Routing and Docs

**Files:**
- Modify: `tests/unit/quic_stream_tests.cpp`
- Modify: `include/flowq/quic/stream.hpp`
- Modify: `docs/development.md`

- [ ] **Step 1: Write failing set routing tests**

Add tests for `stream_receive_set::update_max_data(stream_id, limit)` and `stream_send_set::update_max_data(stream_id, limit)`.

- [ ] **Step 2: Implement set routing helpers**

Follow the existing `state_for(stream_id).method(...)` pattern.

- [ ] **Step 3: Update docs**

Add `## QUIC STREAM flow-control/lifecycle-core scope` after M6 in `docs/development.md`, listing M7 support and explicit non-goals.

- [ ] **Step 4: Full verification**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 }
```

Expected: build succeeds and all CTest tests pass.

## Self-Review

- Spec coverage: The plan maps each M7 design requirement to tests and implementation steps.
- Placeholder scan: No TBD/TODO placeholders are present.
- Type consistency: Proposed methods use existing snake_case style and current stream set routing pattern.
