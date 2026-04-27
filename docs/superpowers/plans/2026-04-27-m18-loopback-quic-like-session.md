# M18 Loopback QUIC-Like Session Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove a basic usable FlowQ QUIC-like loopback session over deterministic in-memory transport and test packet protection.

**Architecture:** Build an integration test harness with two connection loops connected by in-memory datagram queues. Exercise stream send/receive, ACK, loss-triggered stream retransmission, flow control, and close/reset using structural/test packet paths only.

**Tech Stack:** C++20, existing QUIC connection/packet/stream headers, Catch2 integration tests, CMake preset `windows-msvc-vcpkg`.

---

## File Structure

- Create or modify `tests/integration/quic_loopback_tests.cpp`: add end-to-end loopback scenarios.
- Modify `tests/CMakeLists.txt`: register the integration test source if a new file is created.
- Modify `include/flowq/quic/connection.hpp`: add only small test-harness helpers if direct composition is too awkward.
- Modify `docs/development.md`: document M18 as the current basic usable non-production stopping point.

## Task 1: In-Memory Datagram Pump

**Files:**
- Create: `tests/integration/quic_loopback_tests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing integration test**

Create two connection loops, a helper `pump(client, server)` that moves outbound datagrams to peer inbound processing, and assert a client stream write is delivered to the server.

Expected assertions:

```cpp
REQUIRE(client.append_stream_data(0, text("hello")).ok());
pump(client, server);
CHECK(server_delivered_text(server, 0) == "hello");
```

- [ ] **Step 2: Run test to verify RED**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg
```

Expected: integration test does not compile until helpers or CMake registration are added.

- [ ] **Step 3: Implement minimal pump helpers**

Keep helpers in the test file first. Move code into production headers only if multiple tests require the same public seam.

- [ ] **Step 4: Run integration test**

Expected: the loopback stream delivery test passes.

## Task 2: Loss and Retransmission Scenario

**Files:**
- Modify: `tests/integration/quic_loopback_tests.cpp`

- [ ] **Step 1: Write failing test**

Drop the first datagram carrying stream data, deliver later ACK/loss stimuli, and assert the same stream bytes are eventually delivered after retransmission with a new packet number.

- [ ] **Step 2: Run test to verify RED**

Expected: loss pump behavior is not yet modeled in the harness.

- [ ] **Step 3: Implement deterministic loss pump behavior**

Use M14/M15 hooks to mark loss and reschedule stream ranges. Do not add random timing or sockets.

- [ ] **Step 4: Run integration test**

Expected: retransmission loopback test passes.

## Task 3: Flow-Control and Close Scenario

**Files:**
- Modify: `tests/integration/quic_loopback_tests.cpp`

- [ ] **Step 1: Write failing tests**

Add a flow-control scenario where initial credit allows a prefix, a credit update allows the suffix, and final close/reset behavior is observable.

- [ ] **Step 2: Run test to verify RED**

Expected: loopback harness lacks one or more orchestration helpers.

- [ ] **Step 3: Implement minimal orchestration**

Add deterministic helper functions in the test harness to exchange control frames and close/reset events.

- [ ] **Step 4: Run integration tests**

Expected: loopback flow-control and close tests pass.

## Task 4: Documentation and Verification

**Files:**
- Modify: `docs/development.md`

- [ ] **Step 1: Update docs**

Document M18 as a basic usable FlowQ point: deterministic loopback streams, ACK/loss, retransmission, flow control, and close/reset over structural/test protection. Explicitly state it is not production QUIC.

- [ ] **Step 2: Full verification**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 }
```

Expected: build succeeds and all tests pass.

## Self-Review

- Spec coverage: Covers in-memory loopback, stream delivery, loss retransmission, flow control, close/reset, docs.
- Placeholder scan: Real sockets/TLS/interoperability are explicit non-goals.
- Type consistency: Uses existing connection loop, outbound datagram, stream, and packet test seams.
