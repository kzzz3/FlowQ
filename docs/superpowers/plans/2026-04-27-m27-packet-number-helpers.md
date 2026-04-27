# M27 Packet Number Helpers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add RFC 9000 packet-number length selection, truncation, and reconstruction helpers without implementing crypto or real short headers.

**Architecture:** The helpers live in `include/flowq/quic/packet_header.hpp` because they are packet-header arithmetic needed before short-header support. They return existing `flowq::error`-style result structs and remain pure, deterministic, and socket-free.

**Tech Stack:** C++20 header-only code, Catch2 tests, CMake/vcpkg Windows preset.

---

## Files

- Modify: `include/flowq/quic/packet_header.hpp`
- Modify: `tests/unit/quic_packet_header_tests.cpp`
- Modify: `PLAN.md`
- Modify: `README.md`
- Modify: `docs/development.md`
- Modify: `docs/basic-complete.md`

## Task 1: Write failing packet-number tests

- [x] **Step 1: Add tests for packet-number length selection, truncation, and reconstruction**

  Add tests to `tests/unit/quic_packet_header_tests.cpp` that call:

  - `select_packet_number_length(packet_number, largest_acknowledged)`
  - `encode_packet_number(packet_number, length, output)`
  - `decode_packet_number(truncated, length, largest_received)`

- [x] **Step 2: Run focused tests to verify RED**

  ```powershell
  $env:VCPKG_ROOT='D:/vcpkg'
  cmake --build --preset windows-msvc-vcpkg
  ```

  Expected: compile failure because helpers are not defined yet.

## Task 2: Implement minimal helpers

- [x] **Step 1: Add result structs and helper functions**

  Implement packet-number length validation, big-endian truncation to 1-4 bytes, and RFC 9000 reconstruction around `expected = largest_received + 1`.

- [x] **Step 2: Run focused tests to verify GREEN**

  ```powershell
  $env:VCPKG_ROOT='D:/vcpkg'
  cmake --build --preset windows-msvc-vcpkg
  ctest --preset windows-msvc-vcpkg --timeout 10 -R "packet number"
  ```

## Task 3: Sync docs and run full verification

- [x] **Step 1: Update docs**

  Update roadmap/status docs to mark M27 as the first post-basic production-readiness milestone while preserving non-production wording.

- [x] **Step 2: Run full verification**

  ```powershell
  $env:VCPKG_ROOT='D:/vcpkg'
  cmake --preset windows-msvc-vcpkg
  cmake --build --preset windows-msvc-vcpkg
  ctest --preset windows-msvc-vcpkg --timeout 10
  ```

## Self-Review

- Spec coverage: M27 covers packet-number length, truncation, and reconstruction only.
- Placeholder scan: No incomplete placeholders; future milestones are explicitly out of scope for M27.
- Type consistency: All helper names are defined in the packet header public namespace and used directly by tests.
