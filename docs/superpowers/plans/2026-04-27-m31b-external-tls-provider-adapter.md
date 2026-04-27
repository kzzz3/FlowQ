# M31b-a External TLS Provider Surface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a default-off external TLS provider surface for OpenSSL QUIC TLS APIs behind the M31 handshake boundary, without claiming a complete provider-backed handshake.

**Architecture:** FlowQ owns CRYPTO byte routing and state observation only. The provider owns TLS 1.3 transcript processing, certificate validation, key schedule, random generation, QUIC secret export, and provider-specific policy. M31b-a records provider identity/API availability and fails closed when the provider is absent. Full local client/server TLS handshake evidence moves to M31b-b.

**Tech Stack:** C++20 adapter interfaces, CMake/vcpkg default-off backend option, Catch2 integration tests, selected QUIC-capable TLS provider.

---

## Files

- Create: `include/flowq/quic/tls_provider_backend.hpp`
- Create: `tests/unit/quic_tls_provider_backend_tests.cpp`
- Create: `cmake/FlowQTlsBackendOptions.cmake`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `vcpkg.json`
- Modify: `docs/development.md`
- Modify: `docs/basic-complete.md`

## Backend Selection Rules

- Select OpenSSL 3.5+ QUIC TLS APIs through the existing vcpkg `openssl` package.
- Do not add more than one backend in M31b-a.
- Keep the backend CMake option default-off until CI and packaging behavior are proven.
- Record provider name, version/API availability, CMake package, vcpkg package, supported platforms, and unsupported modes in docs.
- Defer complete provider-backed client/server handshake, certificate-policy validation, and key-lifecycle proof to M31b-b.

## Tasks

- [x] Write RED tests proving backend absence produces a disabled provider result, not a false pass.
- [x] Write RED tests proving provider metadata exposes provider name, TLS implementation family, API availability, and enabled cipher suites when the backend is enabled.
- [x] Verify RED before adding the provider surface.
- [x] Add `tls_provider_backend` metadata/status surface and default-off CMake/vcpkg backend option.
- [x] Add compile-time OpenSSL 3.5+ QUIC TLS API checks for `SSL_set_quic_tls_cbs` when the backend is enabled.
- [x] Verify default build, full CTest, install/export, and package-consumer with backend disabled.
- [x] Verify provider-enabled configure/build/tests when local OpenSSL exposes the required QUIC TLS API.
- [x] Request Oracle security/architecture review before M32/M33 interop-sensitive work.

## Acceptance Gate

Default FlowQ builds remain backend-free. Enabling `FLOWQ_ENABLE_OPENSSL_QUIC_TLS` requires OpenSSL 3.5+ QUIC TLS APIs and exposes evidence-bound provider metadata only. Complete provider-backed TLS handshake, certificate-policy validation, key lifecycle proof, and production readiness remain future M31b-b/M33 work.
