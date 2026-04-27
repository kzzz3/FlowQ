# M16 Application Data Structural Packet Space Design

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development for implementation. This stage creates a structural test-only Application packet path; it must not be described as secure or interoperable QUIC.

**Goal:** Allow stream frames to move through a clearly non-production Application packet-number space for local loopback tests.

**Architecture:** M16 extends packet-space handling behind the existing packet protection seam. It may introduce a structural Application packet envelope or test short-header stand-in, but it must label plaintext/null protection as unsafe.

**Tech Stack:** C++20, `packet_header.hpp`, `packet_pipeline.hpp`, `connection.hpp`, Catch2.

---

## Scope

- Add Application packet-number space assembly/parsing sufficient for tests.
- Use explicit test protection only.
- Let `connection_loop` queue, flush, parse, and ACK structural Application packets.
- Preserve Initial/Handshake behavior.

## Behavioral Rules

- Application packet numbers are independent from Initial and Handshake packet numbers.
- Test-protected Application packets can carry STREAM frames in local tests.
- Failure paths still produce explicit errors or close actions.

## Non-Goals

- Real QUIC short-header encoding/decoding.
- Header protection, packet number reconstruction, key phase, TLS 1.3 integration.
- Production wire compatibility.

## Verification

Run packet pipeline and connection tests, then full CTest.
