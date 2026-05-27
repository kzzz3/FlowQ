# M28 Crypto Provider Boundary Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add FlowQ's crypto provider boundary and fail-closed production packet-protection contract without adding a crypto backend.

**Architecture:** Keep cryptographic primitives outside FlowQ. Add value types that describe external provider capabilities and wire those capabilities into existing packet-protection policy checks so production-required paths fail closed.

**Tech Stack:** C++20 header-only interfaces, Catch2, existing `packet_pipeline.hpp`, CMake/vcpkg.

---

## Files

- Create: `include/flowq/quic/crypto_provider.hpp`
- Create: `tests/unit/quic_crypto_provider_tests.cpp`
- Modify: `include/flowq/quic/packet_pipeline.hpp`
- Modify: `tests/unit/quic_packet_pipeline_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/development.md`
- Modify: `PLAN.md`

## Tasks

- [x] Write RED tests proving a missing provider cannot satisfy `packet_protection_policy::production_required`.
- [x] Write RED tests proving plaintext/test-only protection remains `packet_security_level::test_only` even when provider-shaped values exist.
- [x] Write RED tests for provider capability values: HKDF, AEAD seal, AEAD open, header protection, TLS ownership.
- [x] Verify RED with `cmake --build --preset windows-msvc-vcpkg`.
- [x] Add `crypto_provider.hpp` with `cipher_suite`, `crypto_capabilities`, `crypto_provider_status`, and result structs.
- [x] Add packet-pipeline checks requiring authenticated packet protection and provider capabilities under production-required policy.
- [x] Verify GREEN with `ctest --preset windows-msvc-vcpkg --timeout 10 -R "crypto provider|packet protect"`.
- [x] Run full `cmake --preset`, build, CTest, install, and package-consumer gate.
- [x] Document that M28 adds boundaries only and does not add OpenSSL, BoringSSL, AWS-LC, Schannel, QuicTLS, TLS, AEAD, HKDF, or header-protection implementation.
- [x] Request Oracle review before M29.

## Acceptance Gate

Production-required paths fail closed without an external-capability boundary. FlowQ still has no in-tree crypto backend and no production security claim.
