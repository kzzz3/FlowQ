# M19 Crypto Adapter Seam Design

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development for implementation. This stage defines the crypto adapter boundary only; never handwrite production TLS/AEAD/header protection in this milestone.

**Goal:** Make FlowQ’s packet protection boundary explicit, testable, and safe for future integration with an external TLS/crypto library.

**Architecture:** M19 strengthens the existing `packet_protector` seam with capability reporting and adapter contracts. Plaintext/null protection remains available only as an explicit test-only implementation.

**Tech Stack:** C++20, `packet_pipeline.hpp`, `connection.hpp`, Catch2, future external TLS provider boundary.

---

## Scope

- Add explicit security/capability reporting to packet protectors.
- Mark plaintext/null protection as test-only.
- Reject test-only protection on any path labeled production-required.
- Define an external adapter interface shape for future TLS 1.3 / packet protection integration.
- Add fake adapter contract tests.

## Behavioral Rules

- Test-only protection must be visible in type/value names or capability results.
- Production security claims require a non-test adapter and later test-vector/interop work.
- Packet pipeline APIs must not silently treat plaintext as protected QUIC traffic.

## Non-Goals

- TLS handshake, key schedule, AEAD, header protection, certificate validation.
- Integration with OpenSSL/BoringSSL in this milestone.
- Production interoperability claims.

## Verification

Run packet pipeline, connection, and full CTest.
