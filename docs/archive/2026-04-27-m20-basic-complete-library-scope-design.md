# M20 Basic Complete Library Scope Design

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:writing-plans for the execution plan and superpowers:test-driven-development for any future code milestones. M20 itself is a docs/spec freeze; do not implement M21-M26 code in this milestone.

**Goal:** Freeze FlowQ's definition of a basic complete non-production QUIC-like C++ library baseline before adding the public API and productization layers.

**Architecture:** M20 treats the current M19 state as a deterministic protocol-core foundation and defines the remaining library-completion track. It distinguishes M18 “basic usable in-memory loopback” from M26 “basic complete library baseline,” while preserving M19's explicit crypto adapter boundary.

**Tech Stack:** C++20, CMake/vcpkg, standalone Asio, stdexec-direction sender seams, Catch2, future GitHub Actions or equivalent CI.

---

## Scope

- Define FlowQ as a non-production QUIC-like C++20 library baseline, not a production QUIC stack.
- Freeze the M20-M26 basic-complete track: public session API, non-production UDP/ASIO adapter, recovery scheduler adapter, examples, CMake packaging, CI, and release docs.
- Clarify that current low-level types (`connection_loop`, frame queues, packet pipeline, stream state, and ASIO wrappers) remain available but are not the intended final consumer-facing façade.
- Mark the planned public headers for upcoming milestones: `session.hpp`, `events.hpp`, `udp_session.hpp`, and `recovery_scheduler.hpp`.
- Preserve the M19 packet-protection boundary: plaintext protection is test-only and `packet_protection_policy::production_required` rejects it.
- Synchronize `README.md`, `PLAN.md`, `docs/development.md`, and the post-M19 completion plan around the same basic-complete definition.

## Acceptance

- FlowQ is described as a non-production QUIC-like C++ library baseline, not a production QUIC stack.
- Basic complete requires public session API, UDP adapter smoke path, examples, packaging, CI, and docs.
- Basic complete excludes real TLS, AEAD, header protection, congestion control, HTTP/3, and interoperability claims.
- Future production tracks are listed as backlog, not implied current work.

## Basic Complete Definition

FlowQ is basic complete when it is a usable C++20 QUIC-like library baseline that:

- builds and tests from a clean checkout with documented CMake/vcpkg commands;
- exposes a small public API for constructing a non-production QUIC-like session without forcing users to manipulate raw frame queues;
- provides deterministic in-memory and UDP-backed smoke paths for stream data, ACK/loss recovery, flow-control signaling, and reset/close observability;
- keeps packet protection explicit: plaintext is test-only, production-required policy rejects it, and real TLS/AEAD/header protection remains delegated to a future external adapter;
- ships examples, install/export packaging, CI, and documentation that clearly state supported behavior and non-goals.

## Milestone Boundaries

- **M18** was the basic usable in-memory loopback proof.
- **M19** made the crypto adapter seam explicit and prevented test-only protection from satisfying production-required paths.
- **M20** freezes the basic-complete library scope and docs only.
- **M21** adds the public session façade.
- **M22** adds a bounded non-production UDP/ASIO smoke adapter.
- **M23** connects recovery timer values to ASIO scheduling.
- **M24** adds examples and public smoke tests.
- **M25** adds CMake install/export packaging and a package-consumer test.
- **M26** adds CI and declares the basic complete baseline.

## Non-Goals

- RFC-complete or interoperable QUIC.
- Real TLS 1.3 handshake, certificate validation, key schedule, AEAD, or header protection.
- Real short-header 1-RTT packet-number reconstruction.
- Congestion control, pacing, ECN, migration, address validation, Retry integrity, or multipath.
- HTTP/3, WebTransport, 0-RTT, production deployment guidance, or security claims.
- Package-manager publication beyond a local CMake install/export package and consumer test.

## External Reference Alignment

Production QUIC libraries such as MsQuic, quiche, ngtcp2, LSQUIC, and picoquic document public object models, examples, build/install steps, security boundaries, and interop/compliance status. FlowQ should borrow that documentation shape while clearly stating it is not production QUIC, not cryptographically audited, and not expected to interoperate with those stacks.

For packaging, M25 should follow CMake's import/export and package-config guidance: install public headers, export a namespaced target, generate relocatable config/version files, and verify the installed package through a separate consumer project. M20 only freezes that requirement; implementation belongs to M25.

## Verification

- Run a scope-language search across `README.md`, `PLAN.md`, `docs/development.md`, and milestone docs; every production/security mention must be paired with “non-production,” “test-only,” “deferred,” or “future adapter” context.
- Run `git diff --check` for documentation formatting.
- Do not require C++ build/test for M20 unless code files are changed; M20 is docs-only.
