# M19 Crypto Adapter Seam Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Define an explicit adapter boundary for future external TLS 1.3 / QUIC packet protection integration without implementing production crypto in FlowQ.

**Architecture:** Strengthen the existing `packet_protector` seam into a documented crypto adapter interface with capability reporting, protection-level state, and test-only null protection. This milestone is about preventing unsafe overclaiming and preparing for library integration, not about writing AEAD/header protection.

**Tech Stack:** C++20, `packet_pipeline.hpp`, `connection.hpp`, docs, Catch2, CMake preset `windows-msvc-vcpkg`.

---

## File Structure

- Modify `include/flowq/quic/packet_pipeline.hpp`: refine or wrap the existing packet protection interface if needed.
- Modify `include/flowq/quic/connection.hpp`: require explicit test-protection selection for structural Application paths.
- Modify `tests/unit/quic_packet_pipeline_tests.cpp`: add adapter contract tests.
- Modify `tests/unit/quic_connection_tests.cpp`: add tests that unsafe/test protection is explicit.
- Modify `docs/development.md`: document crypto adapter boundary and real TLS deferment.

## Task 1: Make Test Protection Explicit

**Files:**
- Modify: `tests/unit/quic_packet_pipeline_tests.cpp`
- Modify: `include/flowq/quic/packet_pipeline.hpp`

- [x] **Step 1: Write failing tests**

Assert the plaintext/null protector reports a test-only capability and that production-required paths reject it unless a test mode flag is explicit.

Expected assertions:

```cpp
CHECK(protector.security_level() == flowq::quic::packet_security_level::test_only);
CHECK_FALSE(result.ok());
CHECK(result.error.code() == flowq::error_code::protocol_error);
```

- [x] **Step 2: Run test to verify RED**

Expected: the current packet protector does not expose this explicit capability contract.

- [x] **Step 3: Implement capability reporting**

Add a small enum and query method to the protection seam. Preserve existing tests by marking plaintext protection as test-only and updating callers that intentionally use it.

- [x] **Step 4: Run focused tests**

Expected: packet pipeline tests pass.

## Task 2: Add External Adapter Shape Without Implementation

**Files:**
- Modify: `include/flowq/quic/packet_pipeline.hpp`
- Modify: `docs/development.md`

- [x] **Step 1: Write compile-time contract tests**

Add a small fake external adapter in tests that implements the required interface and proves packet pipeline code can call it through the seam.

- [x] **Step 2: Run test to verify RED**

Expected: the refined adapter interface is not yet present.

- [x] **Step 3: Implement interface shape**

Define the adapter methods required for future integration: current protection level, protect, unprotect, and capability/security reporting. Do not add AEAD, TLS handshake, key schedule, or header-protection code.

- [x] **Step 4: Run focused tests**

Expected: fake external adapter tests pass.

## Task 3: Documentation and Verification

**Files:**
- Modify: `docs/development.md`

- [x] **Step 1: Update docs**

Document that M19 is an adapter seam only. State that real QUIC security requires a mature external TLS/crypto library, test vectors, and interop work in later milestones.

- [x] **Step 2: Full verification**

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 }
```

Expected: build succeeds and all tests pass.

## Self-Review

- Spec coverage: Makes null protection explicit, defines external adapter contract, documents real crypto deferment.
- Placeholder scan: Real TLS/AEAD/header protection is deliberately not a hidden task; it is a later external integration.
- Type consistency: Builds on existing `packet_protector`, `protection_level`, and packet pipeline result style.
