# M38 Interop Harness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in interop harness for named mature QUIC implementations without making default CI depend on external peer binaries.

**Architecture:** Scenario parsing and skip behavior are tested by default. Real peer execution is opt-in and records peer name/version before any interop claim.

**Dependency:** Handshake and stream interop scenarios require M31b provider-backed TLS evidence. If the provider backend is missing, those scenarios must skip with an explicit reason.

**Tech Stack:** CMake option, C++ scenario runner shell, JSON scenario files, Catch2 harness self-tests.

---

## Files

- Create: `tests/interop/README.md`
- Create: `tests/interop/flowq_endpoint_driver.cpp`
- Create: `tests/interop/scenarios/basic_handshake.json`
- Create: `tests/interop/scenarios/stream_echo.json`
- Create: `tests/interop/scenarios/loss_recovery.json`
- Modify: `CMakeLists.txt`
- Modify: `docs/development.md`
- Modify: `docs/basic-complete.md`

## Tasks

- [ ] Add `FLOWQ_BUILD_INTEROP` CMake option defaulting off.
- [ ] Write RED self-test for scenario parsing without launching peers.
- [ ] Write RED self-test proving missing peer binaries produce skipped results, not false passes.
- [ ] Write RED self-test proving missing provider-backed TLS adapter produces skipped handshake/stream scenarios, not false passes.
- [ ] Add scenario files with explicit expected packet/event milestones.
- [ ] Verify RED.
- [ ] Implement scenario parser and opt-in runner shell.
- [ ] Verify GREEN and full default CTest with interop disabled.
- [ ] Add docs requiring named peer versions before any interop claim.

## Acceptance Gate

Interop harness is reproducible and opt-in. FlowQ can claim only specific scenario results against named peer versions and named TLS backend versions after those runs pass.
