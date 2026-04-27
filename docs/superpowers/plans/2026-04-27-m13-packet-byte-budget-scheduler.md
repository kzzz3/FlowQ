# M13 Packet Byte Budget Scheduler Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fit complete encoded frame values into caller-provided packet payload budgets before handing them to packet assembly.

**Architecture:** Add a pure frame-budget helper that uses the existing frame encoder to measure each candidate frame, preserves candidate order, and stops before a frame would exceed the payload budget. Keep packet headers, packet-number length choice, AEAD overhead, pacing, and congestion outside this milestone.

**Tech Stack:** C++20, `frame.hpp`, `packet_pipeline.hpp`, `connection.hpp`, Catch2, CMake preset `windows-msvc-vcpkg`.

---

## File Structure

- Modify `include/flowq/quic/packet_pipeline.hpp` or add a small helper near packet planning if existing style favors that file.
- Modify `include/flowq/quic/connection.hpp`: use the helper when selecting queued/scheduled frames for a packet.
- Modify `tests/unit/quic_packet_pipeline_tests.cpp`: add byte-budget helper tests.
- Modify `tests/unit/quic_connection_tests.cpp`: add connection scheduling budget tests.
- Modify `docs/development.md`: document M13 scope.

## Task 1: Select Complete Frames by Encoded Payload Budget

**Files:**
- Modify: `tests/unit/quic_packet_pipeline_tests.cpp`
- Modify: `include/flowq/quic/packet_pipeline.hpp`

- [ ] **Step 1: Write failing tests**

Build a candidate list containing a small `PING`, a `STREAM` frame, and another `PING`. Use a budget that fits only the first two encoded frames. Assert that the helper returns exactly those frames and the consumed encoded size.

Expected assertions:

```cpp
auto result = select_frames_for_payload_budget(candidates, budget);
REQUIRE(result.ok());
CHECK(result.frames.size() == 2);
CHECK(result.encoded_size <= budget);
```

- [ ] **Step 2: Run test to verify RED**

Expected: build fails because `select_frames_for_payload_budget` does not exist.

- [ ] **Step 3: Implement minimal helper**

For each candidate, call the existing frame encoder into a temporary buffer, measure encoded size, append the frame only if it fully fits, and stop at the first non-fitting frame.

- [ ] **Step 4: Run focused tests**

Expected: packet pipeline tests pass.

## Task 2: Apply Byte Budget in Connection Scheduling

**Files:**
- Modify: `tests/unit/quic_connection_tests.cpp`
- Modify: `include/flowq/quic/connection.hpp`

- [ ] **Step 1: Write failing tests**

Schedule multiple stream frames through the connection with a configured payload budget that fits one complete encoded frame. Assert that the first flush contains one frame and the next scheduling call preserves the remaining frame for a later packet.

- [ ] **Step 2: Run test to verify RED**

Expected: connection scheduling does not yet consult encoded frame size.

- [ ] **Step 3: Integrate helper**

Use `select_frames_for_payload_budget()` at the connection scheduling boundary. Do not split individual frames in this milestone.

- [ ] **Step 4: Run focused tests**

Expected: connection and packet pipeline tests pass.

## Task 3: Documentation and Verification

**Files:**
- Modify: `docs/development.md`

- [ ] **Step 1: Update docs**

Document that M13 budgets encoded frames, not UDP datagrams. Exclude packet headers, packet protection overhead, pacing, congestion, and real PMTU behavior.

- [ ] **Step 2: Full verification**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 }
```

Expected: build succeeds and all tests pass.

## Self-Review

- Spec coverage: Measures encoded frame sizes, preserves order, stops before non-fitting frames, integrates at connection boundary.
- Placeholder scan: No packet overhead or PMTU placeholder is hidden; those are explicit later concerns.
- Type consistency: Uses existing `frame` encoding and packet pipeline result style.
