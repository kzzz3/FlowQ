# M31b External TLS Provider Adapter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement a default-off adapter for one vetted QUIC-capable TLS provider behind the M31 handshake boundary.

**Architecture:** FlowQ owns CRYPTO byte routing and state observation only. The selected provider owns TLS 1.3 transcript processing, certificate validation, key schedule, random generation, QUIC secret export, and provider-specific policy. FlowQ records provider identity/version and fails closed when the provider is absent or rejects the handshake.

**Tech Stack:** C++20 adapter interfaces, CMake/vcpkg default-off backend option, Catch2 integration tests, selected QUIC-capable TLS provider.

---

## Files

- Create: `include/flowq/quic/tls_provider_backend.hpp`
- Create: `tests/integration/quic_tls_provider_adapter_tests.cpp`
- Create: `cmake/FlowQTlsBackendOptions.cmake`
- Modify: `include/flowq/quic/tls_handshake.hpp`
- Modify: `include/flowq/quic/key_lifecycle.hpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `vcpkg.json` only after choosing the vetted backend package.
- Modify: `docs/development.md`
- Modify: `docs/basic-complete.md`

## Backend Selection Rules

- Select one vetted QUIC-capable TLS provider before implementation: OpenSSL QUIC APIs, QuicTLS, BoringSSL/AWS-LC, Schannel through a platform abstraction, or another mature provider with QUIC key export support.
- Do not add more than one backend in M31b.
- Keep the backend CMake option default-off until CI and packaging behavior are proven.
- Record provider name, version, CMake package, vcpkg package, supported platforms, and unsupported modes in docs.

## Tasks

- [ ] Write RED tests proving backend absence produces a skipped or disabled provider result, not a false pass.
- [ ] Write RED tests proving provider metadata exposes name, version, TLS implementation family, and enabled cipher suites.
- [ ] Write RED integration test for a local provider-backed client/server handshake reaching `handshake_confirmed` with test certificates owned by the provider layer.
- [ ] Write RED tests proving invalid certificate or invalid verification policy fails closed through provider errors.
- [ ] Write RED tests proving provider key availability events are exported to M33 key lifecycle values without exposing raw key material through public session APIs.
- [ ] Verify RED before linking the backend.
- [ ] Add `tls_provider_backend` interface and default-off CMake/vcpkg backend option.
- [ ] Implement the selected provider adapter by calling provider APIs only; do not implement TLS, HKDF, AEAD, certificate validation, random generation, or key schedule inside FlowQ.
- [ ] Connect provider handshake state to `tls_handshake_adapter` and key availability events to `key_lifecycle`.
- [ ] Verify GREEN with provider-enabled integration tests when the backend is present.
- [ ] Verify default build, full CTest, install/export, and package-consumer with backend disabled.
- [ ] Request Oracle security/architecture review before M32/M33 interop-sensitive work.

## Acceptance Gate

At least one vetted external QUIC-capable TLS provider can complete a local client/server handshake and report key availability through FlowQ boundaries. Invalid certificate or policy failures fail closed. FlowQ still does not implement TLS internals or claim production readiness.
