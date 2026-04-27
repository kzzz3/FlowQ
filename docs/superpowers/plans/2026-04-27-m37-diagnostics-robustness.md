# M37 Diagnostics Robustness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add qlog-style diagnostics, fuzz targets, and sanitizer-capable robustness gates.

**Architecture:** Diagnostics must be optional and behavior-neutral. Fuzz/sanitizer workflows improve evidence but do not create security or production claims by themselves.

**Tech Stack:** C++20 diagnostics values, Catch2, CMake fuzz target options, GitHub Actions robustness workflow.

---

## Files

- Create: `include/flowq/quic/diagnostics.hpp`
- Create: `tests/unit/quic_diagnostics_tests.cpp`
- Create: `.github/workflows/robustness.yml`
- Create: `tests/fuzz/packet_header_fuzz.cpp`
- Create: `tests/fuzz/frame_codec_fuzz.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/development.md`

## Tasks

- [ ] Write RED tests for diagnostics events: packet sent, packet received, packet lost, key updated, congestion state changed, transport parameter decoded.
- [ ] Write RED tests proving diagnostics can be disabled without protocol behavior changes.
- [ ] Add CMake option for fuzz targets defaulting off.
- [ ] Add sanitizer-capable CI workflow for supported hosted runners.
- [ ] Verify RED for diagnostics APIs.
- [ ] Implement event sink interface and compile-only fuzz entry points.
- [ ] Verify GREEN, full default CTest, and local CMake configure with fuzz option when toolchain supports it.

## Acceptance Gate

Diagnostics and robustness gates exist. Docs state fuzzing and sanitizers are evidence inputs, not security certification.
