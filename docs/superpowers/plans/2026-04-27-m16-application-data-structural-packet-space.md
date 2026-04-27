# M16 Application Data Structural Packet Space Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a clearly non-production structural Application Data packet path for local tests and loopback development.

**Architecture:** Extend packet-space handling so stream frames can flow through an Application packet-number space behind the existing packet protection seam. Keep plaintext/test protection explicitly labeled as unsafe and do not claim RFC-complete short-header or 1-RTT support.

**Tech Stack:** C++20, `packet_header.hpp`, `packet_pipeline.hpp`, `connection.hpp`, Catch2, CMake preset `windows-msvc-vcpkg`.

---

## File Structure

- Modify `include/flowq/quic/packet_header.hpp`: add structural Application packet metadata only if needed by packet pipeline.
- Modify `include/flowq/quic/packet_pipeline.hpp`: assemble/parse structural Application packets through `packet_protector` test seam.
- Modify `include/flowq/quic/connection.hpp`: allow queueing/flushing Application-space structural frames under test protection.
- Modify `tests/unit/quic_packet_pipeline_tests.cpp`: add structural Application packet round-trip tests.
- Modify `tests/unit/quic_connection_tests.cpp`: add Application-space connection tests.
- Modify `docs/development.md`: document non-production Application packet boundary.

## Task 1: Structural Application Packet Pipeline

**Files:**
- Modify: `tests/unit/quic_packet_pipeline_tests.cpp`
- Modify: `include/flowq/quic/packet_pipeline.hpp`
- Modify: `include/flowq/quic/packet_header.hpp`

- [ ] **Step 1: Write failing tests**

Assemble a structural Application packet with a plaintext test protector and a STREAM frame, then parse it back and assert packet number and frames round-trip.

Expected assertions:

```cpp
REQUIRE(assembled.ok());
auto parsed = parse_application_packet(assembled.bytes, protector);
REQUIRE(parsed.ok());
CHECK(parsed.packet_number == 0);
REQUIRE(std::holds_alternative<flowq::quic::stream_frame>(parsed.frames[0]));
```

- [ ] **Step 2: Run test to verify RED**

Expected: Application packet assembly/parsing path does not exist or is rejected.

- [ ] **Step 3: Implement structural path**

Add the smallest explicit Application packet envelope needed for deterministic local tests. Name APIs and docs so callers cannot confuse it with secure short-header QUIC.

- [ ] **Step 4: Run focused tests**

Expected: packet pipeline tests pass.

## Task 2: Connection Application Packet Flush/Receive

**Files:**
- Modify: `tests/unit/quic_connection_tests.cpp`
- Modify: `include/flowq/quic/connection.hpp`

- [ ] **Step 1: Write failing tests**

Queue scheduled stream frames into Application packet space, flush to an outbound datagram using plaintext test protection, feed it to a peer connection, and assert stream delivery.

- [ ] **Step 2: Run test to verify RED**

Expected: connection still rejects Application packet number space.

- [ ] **Step 3: Integrate structural Application space**

Allow Application-space packet numbers and trackers in the connection loop only through the explicit structural/test packet path.

- [ ] **Step 4: Run focused tests**

Expected: connection Application packet tests pass.

## Task 3: Documentation and Verification

**Files:**
- Modify: `docs/development.md`

- [ ] **Step 1: Update docs**

Document that M16 is a structural/test Application Data packet seam. State that it is not secure, not interoperable, and not real short-header/header-protected QUIC.

- [ ] **Step 2: Full verification**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 }
```

Expected: build succeeds and all tests pass.

## Self-Review

- Spec coverage: Adds structural Application pipeline, connection flush/receive, and explicit safety boundary.
- Placeholder scan: Real TLS/AEAD/header protection is deferred as a documented security boundary, not a hidden task.
- Type consistency: Reuses `packet_number_space::application`, `packet_protector`, `frame`, and connection action naming.
