# M14 Recovery Timer Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Connect existing pure recovery timing primitives to the connection loop as deterministic timer actions and explicit loss checks.

**Architecture:** Reuse `ack_loss.hpp` recovery helpers from M3b. The connection loop records sent packet metadata, computes loss/PTO deadlines as values, and exposes actions for integration code to arm timers. Timer expiry remains an input event; no real ASIO timer ownership is added here.

**Tech Stack:** C++20, `ack_loss.hpp`, `connection.hpp`, `core.hpp`, Catch2, CMake preset `windows-msvc-vcpkg`.

---

## File Structure

- Modify `include/flowq/quic/connection.hpp`: add recovery timer selection and loss-check entry points.
- Modify `include/flowq/quic/core.hpp`: add or reuse a pure timer action type if connection integration needs a core action value.
- Modify `tests/unit/quic_connection_tests.cpp`: add deterministic timer action/loss tests.
- Modify `docs/development.md`: document M14 recovery integration scope.

## Task 1: Expose Next Recovery Timer Deadline

**Files:**
- Modify: `tests/unit/quic_connection_tests.cpp`
- Modify: `include/flowq/quic/connection.hpp`

- [x] **Step 1: Write failing tests**

After flushing an ack-eliciting packet, ask the connection loop for its next recovery timer and assert a deadline is present. After ACKing that packet, assert no timer remains for that packet number space.

Expected assertions:

```cpp
auto timer = loop.next_recovery_timer(now);
REQUIRE(timer.has_value());
CHECK(timer->space == flowq::quic::packet_number_space::initial);
```

- [x] **Step 2: Run test to verify RED**

Expected: `next_recovery_timer()` does not exist on the connection loop.

- [x] **Step 3: Implement minimal timer selector**

Store enough sent packet timestamps and ack-eliciting metadata to call existing recovery helper functions. Return a value-only deadline object. Preserve the existing `flush()` API with a deterministic `flush(sent_at)` overload for tests.

- [x] **Step 4: Run focused tests**

Expected: connection recovery timer tests pass.

## Task 2: Process Recovery Timer Expiry Structurally

**Files:**
- Modify: `tests/unit/quic_connection_tests.cpp`
- Modify: `include/flowq/quic/connection.hpp`

- [x] **Step 1: Write failing tests**

Create two sent packets, advance time beyond the loss threshold, call `on_recovery_timer(now)`, and assert the result reports packet-level loss values without building probe packets.

- [x] **Step 2: Run test to verify RED**

Expected: timer expiry entry point does not exist.

- [x] **Step 3: Implement minimal expiry handling**

Call existing loss detection helpers, mark packet metadata lost, and return a deterministic loss result. Do not implement congestion, pacing, or PTO probe construction.

- [x] **Step 4: Run focused tests**

Expected: timer expiry tests pass.

## Task 3: Documentation and Verification

**Files:**
- Modify: `docs/development.md`

- [x] **Step 1: Update docs**

Document that M14 wires recovery decisions into connection state but still excludes full RFC 9002 production recovery, persistent congestion, pacing, congestion control, and real timer ownership.

- [x] **Step 2: Full verification**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 }
```

Expected: build succeeds and all tests pass.

## Self-Review

- Spec coverage: Exposes timer deadline, handles timer expiry, returns packet-level loss values, documents exclusions.
- Placeholder scan: No timer implementation is hidden; real ASIO timer ownership is explicitly out of scope.
- Type consistency: Reuses `packet_number_space`, existing ACK/loss metadata, and connection result style.
