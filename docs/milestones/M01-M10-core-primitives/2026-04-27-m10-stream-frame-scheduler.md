# M10 Stream Frame Scheduler Seam Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a pure stream-scope outbound frame scheduler that batches selected `stream_send_set` state into existing `flowq::quic::frame` variants.

**Architecture:** Extend `include/flowq/quic/stream.hpp` with a small result type and a `stream_send_set::pop_frames()` helper. Tests live in `tests/unit/quic_stream_tests.cpp`; docs describe the seam in `docs/development.md`. The scheduler does not assemble packets, touch `connection_loop`, or add TLS/Application Data behavior.

**Tech Stack:** C++20, FlowQ QUIC stream/frame headers, Catch2, CMake preset `windows-msvc-vcpkg`.

---

## File Structure

- Modify `include/flowq/quic/stream.hpp`: add `stream_frame_schedule_result` and `stream_send_set::pop_frames(std::span<const std::uint64_t>, std::size_t, std::size_t)`.
- Modify `tests/unit/quic_stream_tests.cpp`: add M10 stream-set scheduler tests after existing M9 stream send set tests.
- Modify `docs/development.md`: add M10 scheduler seam scope after the M9 section.

## Task 1: Deterministic STREAM Batch Output

**Files:**
- Modify: `tests/unit/quic_stream_tests.cpp`
- Modify: `include/flowq/quic/stream.hpp`

- [ ] **Step 1: Write failing tests**

Add tests that demonstrate the desired API and selected-stream ordering:

```cpp
TEST_CASE("stream send set batches STREAM frames in selected order") {
    flowq::quic::stream_send_set streams{};
    REQUIRE(streams.append(0, text("alpha")).ok());
    REQUIRE(streams.append(4, text("beta")).ok());
    const std::vector<std::uint64_t> order{4, 0};

    auto batch = streams.pop_frames(order, 4, 16);

    REQUIRE(batch.ok());
    REQUIRE(batch.frames.size() == 2);
    REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(batch.frames[0]));
    REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(batch.frames[1]));
    const auto& first = std::get<flowq::quic::stream_frame>(batch.frames[0]);
    const auto& second = std::get<flowq::quic::stream_frame>(batch.frames[1]);
    CHECK(first.stream_id == 4);
    CHECK(as_string(first.data) == "beta");
    CHECK(second.stream_id == 0);
    CHECK(as_string(second.data) == "alpha");
}

TEST_CASE("stream send set respects scheduled frame limit") {
    flowq::quic::stream_send_set streams{};
    REQUIRE(streams.append(0, text("alpha")).ok());
    REQUIRE(streams.append(4, text("beta")).ok());
    const std::vector<std::uint64_t> order{0, 4};

    auto first_batch = streams.pop_frames(order, 1, 16);
    auto second_batch = streams.pop_frames(order, 1, 16);

    REQUIRE(first_batch.ok());
    REQUIRE(second_batch.ok());
    REQUIRE(first_batch.frames.size() == 1);
    REQUIRE(second_batch.frames.size() == 1);
    const auto& first = std::get<flowq::quic::stream_frame>(first_batch.frames[0]);
    const auto& second = std::get<flowq::quic::stream_frame>(second_batch.frames[0]);
    CHECK(first.stream_id == 0);
    CHECK(second.stream_id == 4);
}
```

- [ ] **Step 2: Run tests to verify RED**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg
```

Expected: build fails because `stream_send_set::pop_frames` does not exist and the test file may need `<variant>` included for `std::holds_alternative` / `std::get`.

- [ ] **Step 3: Implement minimal STREAM batching**

Add `<variant>` to the test file if required. In `stream.hpp`, add:

```cpp
struct stream_frame_schedule_result {
    std::vector<frame> frames;
    flowq::error error{};

    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};
```

Then add `stream_send_set::pop_frames()` that returns early for `max_frames == 0`, iterates the provided IDs, appends `frame{result.frame}` when `pop_frame()` returns `has_frame`, propagates `result.error`, and stops at `max_frames`.

- [ ] **Step 4: Run tests to verify GREEN**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 -R quic_stream_tests }
```

Expected: focused stream tests pass.

## Task 2: Blocked Frame Fallback and Edge Cases

**Files:**
- Modify: `tests/unit/quic_stream_tests.cpp`
- Modify: `include/flowq/quic/stream.hpp`

- [ ] **Step 1: Write failing tests**

Add tests for blocked fallback, credit resumption, and empty successful batches:

```cpp
TEST_CASE("stream send set batches STREAM_DATA_BLOCKED for selected blocked stream") {
    flowq::quic::stream_send_set streams{2};
    REQUIRE(streams.append(0, text("hello")).ok());
    const std::vector<std::uint64_t> order{0};
    auto prefix = streams.pop_frames(order, 1, 16);
    REQUIRE(prefix.ok());
    REQUIRE(prefix.frames.size() == 1);

    auto blocked = streams.pop_frames(order, 1, 16);

    REQUIRE(blocked.ok());
    REQUIRE(blocked.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::stream_data_blocked_frame>(blocked.frames[0]));
    const auto& blocked_frame = std::get<flowq::quic::stream_data_blocked_frame>(blocked.frames[0]);
    CHECK(blocked_frame.stream_id == 0);
    CHECK(blocked_frame.maximum_stream_data == 2);
}

TEST_CASE("stream send set emits STREAM after credit update instead of blocked frame") {
    flowq::quic::stream_send_set streams{2};
    REQUIRE(streams.append(0, text("hello")).ok());
    const std::vector<std::uint64_t> order{0};
    REQUIRE(streams.pop_frames(order, 1, 16).frames.size() == 1);
    REQUIRE(streams.pop_frames(order, 1, 16).frames.size() == 1);

    streams.update_max_data(flowq::quic::max_stream_data_frame{0, 5});
    auto resumed = streams.pop_frames(order, 1, 16);

    REQUIRE(resumed.ok());
    REQUIRE(resumed.frames.size() == 1);
    REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(resumed.frames[0]));
    const auto& suffix = std::get<flowq::quic::stream_frame>(resumed.frames[0]);
    CHECK(suffix.stream_id == 0);
    CHECK(suffix.offset == 2);
    CHECK(as_string(suffix.data) == "llo");
}

TEST_CASE("stream send set returns empty successful schedule when no frame is available") {
    flowq::quic::stream_send_set streams{};
    const std::vector<std::uint64_t> empty_order{};
    const std::vector<std::uint64_t> absent_order{8};

    auto no_streams = streams.pop_frames(empty_order, 4, 16);
    auto no_frames_allowed = streams.pop_frames(absent_order, 0, 16);
    auto absent = streams.pop_frames(absent_order, 4, 16);

    REQUIRE(no_streams.ok());
    REQUIRE(no_frames_allowed.ok());
    REQUIRE(absent.ok());
    CHECK(no_streams.frames.empty());
    CHECK(no_frames_allowed.frames.empty());
    CHECK(absent.frames.empty());
}
```

- [ ] **Step 2: Run tests to verify RED**

Run focused stream tests. Expected: at least the blocked fallback test fails because the initial Task 1 implementation emits only STREAM frames and does not query `blocked_frame()` after no STREAM is available.

- [ ] **Step 3: Implement blocked fallback**

Extend `pop_frames()` so that when `pop_frame()` returns no frame and no error, it calls `blocked_frame(stream_id)`. If a blocked frame is present, append `frame{*blocked}` and count it against `max_frames`.

- [ ] **Step 4: Run tests to verify GREEN**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 -R quic_stream_tests }
```

Expected: focused stream tests pass.

## Task 3: Documentation and Full Verification

**Files:**
- Modify: `docs/development.md`

- [ ] **Step 1: Update docs**

Add this section after M9:

```markdown
## QUIC STREAM outbound frame scheduler seam scope

The M10 STREAM scheduler stage adds a pure outbound batching helper to `include/flowq/quic/stream.hpp`. It turns selected stream send states into deterministic vectors of existing structural `flowq::quic::frame` values.

- `stream_send_set::pop_frames()` iterates caller-provided stream IDs in order, making scheduling deterministic and policy-free.
- STREAM data is produced through the existing `stream_send_state::pop_frame()` path, so offsets, FIN behavior, retransmission priority, and stream-level credit enforcement remain centralized.
- When no STREAM data can be emitted for a selected blocked stream, the scheduler can include the existing M9 `STREAM_DATA_BLOCKED` signal value.
- `max_frames` bounds the number of complete frame values returned by one scheduling call; byte-level packet payload budgeting remains future packet-scheduler work.

M10 remains below packet scheduling and connection integration. It does not implement encoded frame byte-budget fitting, `MAX_DATA`, `DATA_BLOCKED`, connection-level flow control, `connection_loop` integration, packet-type legality, Application Data packets, short headers, TLS, AEAD/header protection, congestion control, RESET_STREAM, STOP_SENDING, prioritization policy beyond caller order, blocked-frame deduplication, or public APIs.
```

- [ ] **Step 2: Full verification**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 }
```

Expected: build succeeds and all tests pass.

- [ ] **Step 3: Oracle review**

Ask Oracle to review the M10 seam for RFC/scope bugs before committing. Include modified files and verification output in the prompt.

## Self-Review

- Spec coverage: Tasks cover deterministic STREAM batching, frame limits, blocked fallback, credit resumption, empty edge cases, docs, verification, and Oracle review.
- Placeholder scan: No TBD/TODO placeholders are present.
- Type consistency: The planned method signature, result type, frame variants, and test helper names match existing FlowQ stream code.
