# M8 Flow-Control Frame Codec Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add structural encode/decode support for four QUIC byte-credit flow-control frames.

**Architecture:** Extend the existing header-only frame codec in `include/flowq/quic/frame.hpp` and add focused Catch2 coverage in `tests/unit/quic_frame_tests.cpp`. Keep M8 codec-only; runtime flow-control policy remains future work.

**Tech Stack:** C++20, existing QUIC varint helpers, Catch2, CMake preset `windows-msvc-vcpkg`.

---

## File Structure

- Modify `include/flowq/quic/frame.hpp`: add frame structs, variant alternatives, encode helpers/overloads, and decode branches.
- Modify `tests/unit/quic_frame_tests.cpp`: add positive, mixed, truncation, and unsupported-boundary tests.
- Modify `docs/development.md`: add M8 codec-only scope and non-goals after M7.

## Task 1: Positive Flow-Control Frame Round Trips

**Files:**
- Modify: `tests/unit/quic_frame_tests.cpp`
- Modify: `include/flowq/quic/frame.hpp`

- [ ] **Step 1: Write failing tests**

Add round-trip tests for:

```cpp
flowq::quic::max_data_frame{1024};
flowq::quic::max_stream_data_frame{4, 2048};
flowq::quic::data_blocked_frame{4096};
flowq::quic::stream_data_blocked_frame{8, 8192};
```

Assert `encode_frame(...).ok()`, `decode_frames(...).ok()`, exactly one frame, matching variant type, and matching fields.

- [ ] **Step 2: Run tests to verify RED**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 -R "flow-control frame" }
```

Expected: build fails because M8 frame structs/overloads are not defined.

- [ ] **Step 3: Implement minimal positive codec**

Add the four structs, extend the `frame` variant, add encode overloads, and decode `0x10`, `0x11`, `0x14`, and `0x15`.

- [ ] **Step 4: Run tests to verify GREEN**

Run the selected tests. Expected: PASS.

## Task 2: Mixed Decode Sequence

**Files:**
- Modify: `tests/unit/quic_frame_tests.cpp`
- Modify: `include/flowq/quic/frame.hpp` if Task 1 left decoder advancement bugs.

- [ ] **Step 1: Write failing test**

Add a mixed decode test with `PING`, `MAX_DATA`, `MAX_STREAM_DATA`, `DATA_BLOCKED`, and `STREAM_DATA_BLOCKED` in one payload.

- [ ] **Step 2: Run selected test to verify RED/GREEN**

If it fails, fix decoder offset advancement; otherwise record it as already green under Task 1 implementation.

## Task 3: Truncation and Unsupported Boundaries

**Files:**
- Modify: `tests/unit/quic_frame_tests.cpp`
- Modify: `include/flowq/quic/frame.hpp` if malformed cases do not fail correctly.

- [ ] **Step 1: Write failing malformed tests**

Add tests that these inputs fail:

```cpp
bytes({0x10});
bytes({0x10, 0x40});
bytes({0x11});
bytes({0x11, 0x04});
bytes({0x11, 0x04, 0x40});
bytes({0x14});
bytes({0x15});
bytes({0x15, 0x08});
bytes({0x15, 0x08, 0x40});
```

Add a boundary test that `0x12`, `0x13`, `0x16`, and `0x17` remain unsupported in M8.

- [ ] **Step 2: Run selected tests to verify GREEN**

Run malformed tests. Expected: PASS with `ok() == false` for malformed/unsupported inputs.

## Task 4: Documentation and Full Verification

**Files:**
- Modify: `docs/development.md`

- [ ] **Step 1: Update development docs**

Add an M8 section documenting the four structural flow-control frame codecs and explicit non-goals.

- [ ] **Step 2: Full verification**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 }
```

Expected: build succeeds and all CTest tests pass.

## Self-Review

- Spec coverage: Positive, mixed, malformed, unsupported-boundary, and docs tasks cover the M8 design.
- Placeholder scan: No TBD/TODO placeholders are present.
- Type consistency: Frame names and fields match the design and RFC 9000 field order.
