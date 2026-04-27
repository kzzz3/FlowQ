# FlowQ Post-Basic Production Readiness Design

## Goal

Move FlowQ from a basic-complete non-production C++20 QUIC-like library baseline toward production QUIC readiness through evidence-backed, separately reviewable milestones.

## Production Readiness Principle

FlowQ must not claim production QUIC, RFC compliance, wire interoperability, or security until those claims are backed by executable tests, external crypto/TLS integration, and interop evidence. Plaintext/test-only packet protection remains non-production.

## Milestone Roadmap

### M27: RFC 9000 Packet Number Helpers

Add pure packet-number length selection, truncation, and reconstruction helpers. This is required before real short-header decoding and header protection integration. It is safe to implement now because it is deterministic arithmetic and does not implement cryptography.

### M28: Crypto Provider and Packet Protection Interface

Define a provider boundary for vetted crypto libraries. FlowQ may call external HKDF, AEAD, and header-protection primitives through an interface, but it must not implement those primitives itself.

### M29: RFC 9001 Initial Packet Protection Vectors

Use a vetted backend through the M28 provider boundary to pass selected RFC 9001 packet-protection vectors. This milestone proves a narrow evidence claim only: selected vectors pass.

### M30: Transport Parameter Codec

Add structural transport parameter encode/decode and validation, then map safe parameters into connection/session configuration. TLS extension binding remains separate.

### M31: TLS Handshake Adapter Boundary

Introduce a TLS handshake adapter that exchanges CRYPTO bytes, exposes handshake state, and reports key availability. Certificate validation and TLS internals stay in the external backend.

### M31b: External TLS Provider Adapter Implementation

Implement one default-off adapter for a vetted QUIC-capable TLS provider behind the M31 boundary. The provider owns TLS 1.3 transcript processing, certificate validation, key schedule, random generation, and QUIC secret export. FlowQ records provider identity/version and fails closed when the provider is absent or rejects the handshake.

### M32: RFC-shaped Short Header Skeleton

Add short-header value types and parsing/encoding after packet-number helpers exist. Header protection and 1-RTT AEAD remain behind the M28/M31 boundaries.

### M33: Congestion Control Baseline

Add a deterministic congestion-controller interface and NewReno-style model tests. Keep recovery arithmetic independent from ASIO scheduling.

### M34: Interop Harness Preparation

Add offline vector tests, qlog-style diagnostics, and a harness shape for future interop against mature QUIC stacks. Do not claim interoperability until automated interop passes.

## Explicit Non-Goals

- No hand-written AES, ChaCha20, Poly1305, HKDF, TLS 1.3, certificate validation, or random number generation.
- No production readiness claims from partial milestones.
- No one-shot rewrite of connection, crypto, recovery, and UDP layers.
- No HTTP/3, WebTransport, QPACK, or application-layer protocol work until transport production readiness has evidence.

## First Execution Scope

M27 is the first implementation milestone. It adds only RFC 9000 packet-number helper functions and tests. It does not decode real short headers, apply header protection, or change the current structural Application packet path.

## Complete Route Document

The full post-basic production-readiness route is captured in `docs/superpowers/plans/2026-04-27-post-basic-production-readiness-roadmap.md`. That route expands the brief M28-M34 list above into M28-M39, including M31b external TLS provider integration, with files, tests, acceptance gates, and wording constraints.
