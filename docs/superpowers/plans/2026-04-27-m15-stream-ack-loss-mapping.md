# M15 Stream ACK/Loss Mapping Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Track which STREAM ranges are carried by sent packets and map packet ACK/loss outcomes back to stream send state.

**Architecture:** Add a sent-packet stream-range ledger in the connection layer. On packet ACK, call `stream_send_state::on_acked()` for carried ranges; on packet loss, call `stream_send_state::on_lost()` so M10 scheduling can retransmit the same stream bytes in future packets.

**Tech Stack:** C++20, `connection.hpp`, `stream.hpp`, existing ACK/loss trackers, Catch2, CMake preset `windows-msvc-vcpkg`.

---

## File Structure

- Modify `include/flowq/quic/connection.hpp`: add packet-to-stream range ledger and ACK/loss callbacks.
- Modify `include/flowq/quic/stream.hpp`: expose set-level `on_acked(stream_id, range)` and `on_lost(stream_id, range)` if not already available.
- Modify `tests/unit/quic_connection_tests.cpp`: add ACK/loss mapping integration tests.
- Modify `tests/unit/quic_stream_tests.cpp`: add set-level ACK/loss routing tests if new wrappers are added.
- Modify `docs/development.md`: document M15 scope.

## Task 1: Record STREAM Ranges on Sent Packets

**Files:**
- Modify: `tests/unit/quic_connection_tests.cpp`
- Modify: `include/flowq/quic/connection.hpp`

- [x] **Step 1: Write failing tests**

Schedule and flush a STREAM frame, then inspect a test-visible ledger snapshot showing packet number, stream ID, offset, length, and FIN flag.

Expected assertions:

```cpp
auto ranges = loop.sent_stream_ranges(packet_number_space::initial, 0);
REQUIRE(ranges.size() == 1);
CHECK(ranges[0].stream_id == 0);
CHECK(ranges[0].offset == 0);
CHECK(ranges[0].length == 5);
```

- [x] **Step 2: Run test to verify RED**

Expected: no sent stream range ledger exists.

- [x] **Step 3: Implement ledger recording**

When STREAM frames are included in a sent packet, record their stream ranges keyed by packet number space and packet number.

- [x] **Step 4: Run focused tests**

Expected: ledger tests pass.

## Task 2: Map ACK and Loss Back to Stream State

**Files:**
- Modify: `tests/unit/quic_connection_tests.cpp`
- Modify: `include/flowq/quic/connection.hpp`
- Modify: `include/flowq/quic/stream.hpp`

- [x] **Step 1: Write failing tests**

Send stream data, mark the packet lost through ACK-threshold and recovery-timer paths, schedule again, and assert the retransmitted STREAM frame carries the same stream ID, offset, and bytes. Then ACK the sent stream information and assert a later loss signal does not reschedule already acknowledged data.

- [x] **Step 2: Run test to verify RED**

Expected: packet loss does not yet feed stream retransmission state.

- [x] **Step 3: Implement mapping callbacks**

On packet ACK, call stream set ACK routing. On packet loss from ACK-threshold or recovery timer paths, call stream set loss routing. Preserve packet-level ACK/loss tracker behavior. Retransmitted lost STREAM ranges must bypass fresh connection credit accounting, late ACKs for lost FIN ranges must suppress FIN retransmission, and manually queued STREAM frames must not create send state during outcome routing.

- [x] **Step 4: Run focused tests**

Expected: stream retransmission mapping tests pass.

## Task 3: Documentation and Verification

**Files:**
- Modify: `docs/development.md`

- [x] **Step 1: Update docs**

Document that M15 retransmits stream information, not packet numbers. Exclude congestion, prioritization, RESET_STREAM, STOP_SENDING, and full packet recovery policy beyond existing helpers.

- [x] **Step 2: Full verification**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 }
```

Expected: build succeeds and all tests pass.

## Self-Review

- Spec coverage: Records sent stream ranges, maps ACK/loss to stream state, proves retransmission bytes are stable, covers connection-credit-exhausted retransmission, late FIN ACK suppression after empty and partial data retransmission, unsent empty FIN loss rejection, manual STREAM ACK/loss safety for missing and existing unsent send state, packet-budget splitting, and Application-space ledger boundaries.
- Placeholder scan: No retransmission policy placeholder is hidden; this is a ledger integration milestone.
- Type consistency: Uses existing `stream_send_range`, packet-number-space names, and stream set routing style.
