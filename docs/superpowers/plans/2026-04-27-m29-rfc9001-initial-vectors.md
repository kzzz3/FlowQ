# M29 RFC 9001 Initial Vector Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Pass selected RFC 9001 Initial packet-protection vectors through vetted external primitives selected by CMake.

**Architecture:** Keep FlowQ's core backend-agnostic. Add an optional crypto backend adapter behind M28 provider interfaces and prove only selected vector correctness.

**Tech Stack:** C++20, Catch2, CMake options, vcpkg package for the selected vetted backend, RFC 9001 Appendix A vectors.

---

## Files

- Create: `include/flowq/quic/initial_keys.hpp`
- Create: `tests/unit/quic_initial_keys_tests.cpp`
- Create: `cmake/FlowQCryptoBackendOptions.cmake`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `vcpkg.json` only after choosing a vetted backend package.
- Modify: `docs/development.md`
- Modify: `docs/basic-complete.md`

## Tasks

- [x] Select one vetted backend for the first vector implementation and record why it is acceptable on Windows/vcpkg.
- [x] Write RED tests for RFC 9001 Initial salt and client/server Initial secret derivation inputs.
- [x] Write RED tests for selected RFC 9001 key, IV, and header-protection-key outputs.
- [x] Write RED tests proving altered associated data or ciphertext is rejected by vector-backed open.
- [x] Verify RED before linking a backend adapter.
- [x] Add a default-off CMake option for the selected backend.
- [x] Add provider calls to external HKDF, AEAD seal/open, and header-protection primitives.
- [x] Keep plaintext protector separate and test-only.
- [x] Verify GREEN with vector tests and full CTest.
- [x] Run install/package-consumer gate with backend disabled and, where available, backend enabled.
- [x] Request Oracle security review.

## Acceptance Gate

Selected RFC 9001 vectors pass through vetted external primitives. Public docs say “passes selected vectors,” not “secure,” “interoperable,” or “production-ready.”
