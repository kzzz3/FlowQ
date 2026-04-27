# FlowQ Post-Basic Production Readiness Roadmap

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move FlowQ from a basic-complete non-production QUIC-like baseline toward production-grade QUIC through narrow, evidence-backed milestones.

**Architecture:** Production readiness is split by protocol responsibility: crypto provider boundary, RFC vectors, transport parameters, TLS handshake adapter, short headers, key lifecycle, recovery/congestion, endpoint routing, diagnostics, interop, and release gates. Each milestone must preserve the current non-production status until its evidence supports a narrower claim.

**Tech Stack:** C++20 header-only FlowQ core, Catch2, CMake/vcpkg, Asio, stdexec, vetted external crypto/TLS libraries selected through narrow provider interfaces, GitHub Actions.

---

## Global Rules

- Do not write AES, ChaCha20, Poly1305, HKDF, TLS 1.3, certificate validation, random number generation, or cryptographic side-channel-sensitive code inside FlowQ.
- Do not describe FlowQ as production-ready, secure, interoperable, or RFC-compliant until the production release gate says so with evidence.
- Keep plaintext/test-only protection available only for deterministic tests and examples.
- Keep every milestone independently reviewable with TDD RED/GREEN evidence.
- Request Oracle review after every milestone that touches crypto, TLS, key lifecycle, public status wording, or interop claims.
- Keep CMake install/export and package-consumer verification green after each public header change.

## Milestone Index

- [x] M27: RFC 9000 packet-number truncation and reconstruction helpers.
- [x] M28: Crypto provider boundary and fail-closed packet protection contract.
- [x] M29: RFC 9001 Initial packet-protection vectors through vetted primitives.
- [x] M30: Transport parameter codec and config mapping.
- [ ] M31: TLS handshake adapter boundary and CRYPTO byte pump.
- [ ] M31b: External TLS provider adapter implementation.
- [ ] M32: RFC-shaped short-header value model and parser shell.
- [ ] M33: Key lifecycle gates and packet-space discard rules.
- [ ] M34: Recovery and congestion-control production baseline.
- [ ] M35: Connection ID routing, version negotiation, Retry, and address-validation preparation.
- [ ] M36: Production UDP endpoint lifecycle and public API hardening.
- [ ] M37: Diagnostics, qlog-style events, fuzzing, and sanitizer gates.
- [ ] M38: Interop harness against mature QUIC implementations.
- [ ] M39: Production release evidence gate and status wording review.

## Individual Plan Files

- M28: `docs/superpowers/plans/2026-04-27-m28-crypto-provider-boundary.md`
- M29: `docs/superpowers/plans/2026-04-27-m29-rfc9001-initial-vectors.md`
- M30: `docs/superpowers/plans/2026-04-27-m30-transport-parameters.md`
- M31: `docs/superpowers/plans/2026-04-27-m31-tls-handshake-adapter.md`
- M31b: `docs/superpowers/plans/2026-04-27-m31b-external-tls-provider-adapter.md`
- M32: `docs/superpowers/plans/2026-04-27-m32-short-header-shell.md`
- M33: `docs/superpowers/plans/2026-04-27-m33-key-lifecycle.md`
- M34: `docs/superpowers/plans/2026-04-27-m34-congestion-baseline.md`
- M35: `docs/superpowers/plans/2026-04-27-m35-routing-version-retry.md`
- M36: `docs/superpowers/plans/2026-04-27-m36-endpoint-lifecycle.md`
- M37: `docs/superpowers/plans/2026-04-27-m37-diagnostics-robustness.md`
- M38: `docs/superpowers/plans/2026-04-27-m38-interop-harness.md`
- M39: `docs/superpowers/plans/2026-04-27-m39-production-evidence-gate.md`

## M28: Crypto Provider Boundary and Fail-Closed Packet Protection Contract

**Goal:** Define provider-facing interfaces for HKDF, AEAD, and header protection without adding a backend or implementing crypto in FlowQ.

**Files:**
- Create: `include/flowq/quic/crypto_provider.hpp`
- Create: `tests/unit/quic_crypto_provider_tests.cpp`
- Modify: `include/flowq/quic/packet_pipeline.hpp`
- Modify: `tests/unit/quic_packet_pipeline_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/development.md`
- Modify: `PLAN.md`

**TDD steps:**
- [ ] Write tests proving a missing provider cannot satisfy `packet_protection_policy::production_required`.
- [ ] Write tests proving provider capability values can represent HKDF, AEAD seal/open, header protection, and TLS ownership as external capabilities.
- [ ] Write tests proving plaintext/test-only protection stays test-only even when provider-like values exist.
- [ ] Verify RED with `cmake --build --preset windows-msvc-vcpkg`.
- [ ] Add `crypto_provider.hpp` value types: `crypto_capabilities`, `cipher_suite`, `crypto_provider_status`, and result structs for provider availability.
- [ ] Wire packet-pipeline production checks so production-required paths fail closed unless both packet protector security and provider capability requirements are satisfied.
- [ ] Verify GREEN with `ctest --preset windows-msvc-vcpkg --timeout 10 -R "crypto provider|packet protect"`.

**Acceptance gate:** Full CTest passes, package-consumer still builds, docs state that M28 adds boundaries only and no crypto backend.

## M29: RFC 9001 Initial Packet-Protection Vectors Through Vetted Primitives

**Goal:** Add a selectable external crypto backend adapter and pass selected RFC 9001 Initial packet-protection vectors.

**Files:**
- Create: `include/flowq/quic/initial_keys.hpp`
- Create: `tests/unit/quic_initial_keys_tests.cpp`
- Create: `cmake/FlowQCryptoBackendOptions.cmake`
- Modify: `CMakeLists.txt`
- Modify: `vcpkg.json` only after choosing the vetted backend package.
- Modify: `docs/development.md`
- Modify: `docs/basic-complete.md`

**TDD steps:**
- [ ] Write tests for RFC 9001 Initial salt, client/server Initial secret derivation inputs, and expected key/iv/header-protection-key bytes from selected RFC vectors.
- [ ] Write tests proving vector-backed packet seal/open rejects altered associated data and altered ciphertext.
- [ ] Verify RED before adding the backend adapter.
- [ ] Add a backend option that defaults off unless the dependency is present through vcpkg/CMake.
- [ ] Implement only provider calls to external HKDF, AEAD, and header protection primitives.
- [ ] Verify GREEN with vector tests and full CTest.

**Acceptance gate:** Selected RFC 9001 vectors pass through a vetted backend. Docs say “passes selected vectors,” not “secure” or “production-ready.”

## M30: Transport Parameter Codec and Config Mapping

**Goal:** Encode/decode core QUIC transport parameters and map safe values into FlowQ session/connection config.

**Files:**
- Create: `include/flowq/quic/transport_parameters.hpp`
- Create: `tests/unit/quic_transport_parameters_tests.cpp`
- Modify: `include/flowq/quic/session.hpp`
- Modify: `include/flowq/quic/connection.hpp`
- Modify: `tests/unit/quic_session_tests.cpp`
- Modify: `tests/unit/quic_connection_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/development.md`

**TDD steps:**
- [x] Write round-trip tests for `initial_max_data`, `initial_max_stream_data_bidi_local`, `initial_max_stream_data_bidi_remote`, `initial_max_stream_data_uni`, `max_idle_timeout`, `max_udp_payload_size`, `active_connection_id_limit`, and `disable_active_migration`.
- [x] Write malformed-input tests for duplicate parameters, truncated varints, unknown preserved parameters, and invalid values.
- [x] Verify RED.
- [x] Implement structural transport parameter value types and codec using existing varint helpers.
- [x] Map selected decoded parameters into connection/session config without TLS binding.
- [x] Verify GREEN and full CTest.

**Acceptance gate:** Structural codec is deterministic, malformed inputs fail with `protocol_error`, and docs state TLS extension binding remains M31 scope.

## M31: TLS Handshake Adapter Boundary and CRYPTO Byte Pump

**Goal:** Define the interface between FlowQ CRYPTO frames and an external TLS 1.3 QUIC-capable provider.

**Files:**
- Create: `include/flowq/quic/tls_handshake.hpp`
- Create: `tests/unit/quic_tls_handshake_tests.cpp`
- Modify: `include/flowq/quic/frame.hpp`
- Modify: `include/flowq/quic/connection.hpp`
- Modify: `include/flowq/quic/session.hpp`
- Modify: `tests/unit/quic_frame_tests.cpp`
- Modify: `tests/unit/quic_connection_tests.cpp`
- Modify: `tests/unit/quic_session_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/development.md`

**TDD steps:**
- [ ] Write tests for CRYPTO byte buffering by Initial, Handshake, and Application encryption levels.
- [ ] Write tests for adapter state transitions: `idle`, `handshaking`, `handshake_confirmed`, `failed`.
- [ ] Write tests proving application data cannot be sent under production-required policy before handshake confirmation and key availability.
- [ ] Verify RED.
- [ ] Implement `tls_handshake_adapter` interface and deterministic fake adapter for tests.
- [ ] Route CRYPTO frame bytes between connection/session and the adapter without implementing TLS internals.
- [ ] Verify GREEN and full CTest.

**Acceptance gate:** FlowQ can model TLS handshake byte flow and state but still delegates TLS internals and certificate validation to external code.

## M31b: External TLS Provider Adapter Implementation

**Goal:** Implement a default-off adapter for one vetted QUIC-capable TLS provider behind the M31 handshake boundary.

**Files:**
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

**TDD steps:**
- [ ] Write tests proving backend absence produces a skipped or disabled provider result, not a false pass.
- [ ] Write tests proving provider metadata exposes name, version, TLS implementation family, and enabled cipher suites.
- [ ] Write provider-backed local handshake tests reaching `handshake_confirmed` with test certificates owned by the provider layer.
- [ ] Write invalid certificate or invalid verification policy tests that fail closed through provider errors.
- [ ] Verify RED before linking the backend.
- [ ] Implement one selected provider adapter by calling provider APIs only; do not implement TLS, HKDF, AEAD, certificate validation, random generation, or key schedule inside FlowQ.
- [ ] Connect provider handshake state to M31 and key availability events to M33.
- [ ] Verify default build with backend disabled and provider-enabled tests when the backend is present.

**Acceptance gate:** A vetted external QUIC-capable TLS provider can complete a local handshake and report key availability through FlowQ boundaries. FlowQ still does not implement TLS internals or claim production readiness.

## M32: RFC-Shaped Short-Header Value Model and Parser Shell

**Goal:** Add short-header value types and parser/encoder shell that integrates M27 packet-number helpers but does not remove header protection by itself.

M32 depends on M31b for production-path packet protection and key availability evidence. Without a configured M31b backend, provider-backed short-header tests must skip rather than pass falsely.

**Files:**
- Modify: `include/flowq/quic/packet_header.hpp`
- Modify: `include/flowq/quic/packet_pipeline.hpp`
- Modify: `tests/unit/quic_packet_header_tests.cpp`
- Modify: `tests/unit/quic_packet_pipeline_tests.cpp`
- Modify: `docs/development.md`

**TDD steps:**
- [ ] Write tests for short-header structural fields: fixed bit, spin bit preservation, key phase, destination connection ID, packet-number length, protected payload bytes.
- [ ] Write tests proving parsing protected short headers requires caller-provided destination connection ID length and header-protection context.
- [ ] Write tests proving short-header parsing fails closed when header protection context is absent under production policy.
- [ ] Verify RED.
- [ ] Add `short_header` value type and parser shell using M27 helpers.
- [ ] Keep structural Application envelope separate and clearly non-production.
- [ ] Verify GREEN and full CTest.

**Acceptance gate:** Short-header shape exists for future packet protection integration, but docs avoid claiming real 1-RTT support.

## M33: Key Lifecycle Gates and Packet-Space Discard Rules

**Goal:** Model key availability, installation, discard, and packet-space lifecycle gates for Initial, Handshake, 0-RTT, and 1-RTT.

**Files:**
- Create: `include/flowq/quic/key_lifecycle.hpp`
- Create: `tests/unit/quic_key_lifecycle_tests.cpp`
- Modify: `include/flowq/quic/connection.hpp`
- Modify: `include/flowq/quic/session.hpp`
- Modify: `tests/unit/quic_connection_tests.cpp`
- Modify: `tests/unit/quic_session_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/development.md`

**TDD steps:**
- [ ] Write tests for installing Initial, Handshake, and 1-RTT send/receive key availability as value events.
- [ ] Write tests for discarding Initial keys after Handshake keys become available.
- [ ] Write tests for discarding Handshake keys after handshake confirmation.
- [ ] Write tests proving lost-packet and ACK state tied to discarded packet spaces is removed or ignored safely.
- [ ] Verify RED.
- [ ] Implement key lifecycle values and connection/session gating.
- [ ] Verify GREEN and full CTest.

**Acceptance gate:** Key lifecycle policy is deterministic and testable; external TLS still provides real secrets and key material.

## M34: Recovery and Congestion-Control Production Baseline

**Goal:** Add bytes-in-flight accounting and a deterministic NewReno-style congestion controller while preserving existing recovery tests.

**Files:**
- Create: `include/flowq/quic/congestion.hpp`
- Create: `tests/unit/quic_congestion_tests.cpp`
- Modify: `include/flowq/quic/ack_loss.hpp`
- Modify: `include/flowq/quic/connection.hpp`
- Modify: `tests/unit/quic_ack_loss_tests.cpp`
- Modify: `tests/unit/quic_connection_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/development.md`

**TDD steps:**
- [ ] Write tests for bytes-in-flight increase on ack-eliciting send and decrease on ACK/loss.
- [ ] Write tests for slow start growth, congestion avoidance growth, loss reduction, and persistent-congestion signal handling.
- [ ] Write tests proving loss detection remains per packet-number space while congestion state is path-level.
- [ ] Verify RED.
- [ ] Implement `congestion_controller` interface and deterministic NewReno-style model.
- [ ] Integrate connection send/ACK/loss paths without adding pacing timers.
- [ ] Verify GREEN and full CTest.

**Acceptance gate:** Deterministic congestion behavior passes tests; pacing and production tuning remain later work.

## M35: Connection ID Routing, Version Negotiation, Retry, and Address-Validation Preparation

**Goal:** Prepare server-side production routing primitives without claiming a production listener.

**Files:**
- Create: `include/flowq/quic/connection_routing.hpp`
- Create: `tests/unit/quic_connection_routing_tests.cpp`
- Modify: `include/flowq/quic/packet_header.hpp`
- Modify: `include/flowq/quic/connection.hpp`
- Modify: `tests/unit/quic_packet_header_tests.cpp`
- Modify: `tests/unit/quic_connection_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/development.md`

**TDD steps:**
- [ ] Write tests for destination connection ID lookup, unknown connection ID handling, and connection ID retirement bookkeeping.
- [ ] Write tests for Version Negotiation packet selection from supported versions.
- [ ] Write tests for Retry token validation interface shape and Retry integrity delegation to crypto provider.
- [ ] Verify RED.
- [ ] Implement routing table value types and version/retry decision helpers.
- [ ] Verify GREEN and full CTest.

**Acceptance gate:** Routing and Retry preparation is deterministic; no production server listener is claimed.

## M36: Production UDP Endpoint Lifecycle and Public API Hardening

**Goal:** Turn the current bounded UDP smoke adapter into a production-shaped endpoint API with explicit lifecycle, ownership, and failure boundaries.

**Files:**
- Create: `include/flowq/quic/endpoint_driver.hpp`
- Create: `tests/integration/quic_endpoint_driver_tests.cpp`
- Modify: `include/flowq/quic/udp_session.hpp`
- Modify: `include/flowq/quic/session.hpp`
- Modify: `tests/integration/quic_udp_session_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/development.md`

**TDD steps:**
- [ ] Write tests for caller-owned socket lifecycle and explicit stop behavior.
- [ ] Write tests for datagram receive routing by connection ID through M35 routing helpers.
- [ ] Write tests for bounded send queue behavior and error reporting under ASIO failures.
- [ ] Write tests proving test-only plaintext sessions cannot be constructed through production endpoint builders.
- [ ] Verify RED.
- [ ] Implement endpoint driver interfaces and harden public construction paths.
- [ ] Verify GREEN and full CTest.

**Acceptance gate:** Endpoint driver is production-shaped but still gated by crypto/TLS/interop evidence before production claims.

## M37: Diagnostics, qlog-Style Events, Fuzzing, and Sanitizer Gates

**Goal:** Add observability and robustness gates needed before interop and production review.

**Files:**
- Create: `include/flowq/quic/diagnostics.hpp`
- Create: `tests/unit/quic_diagnostics_tests.cpp`
- Create: `.github/workflows/robustness.yml`
- Create: `tests/fuzz/packet_header_fuzz.cpp`
- Create: `tests/fuzz/frame_codec_fuzz.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/development.md`

**TDD steps:**
- [ ] Write tests for qlog-style event values for packet sent, packet received, packet lost, key updated, congestion state changed, and transport parameter decoded.
- [ ] Write tests proving diagnostics can be disabled without changing protocol behavior.
- [ ] Add fuzz targets for packet header and frame codec entry points.
- [ ] Add sanitizer-capable CI workflow for supported hosted runners.
- [ ] Verify RED for new diagnostics APIs.
- [ ] Implement diagnostics event sink interface and compile fuzz targets.
- [ ] Verify GREEN, full CTest, and CI workflow syntax via local CMake configure where possible.

**Acceptance gate:** Diagnostics and robustness workflows exist; no interop or production security claim is made from fuzzing alone.

## M38: Interop Harness Against Mature QUIC Implementations

**Goal:** Add an opt-in harness that can drive FlowQ against mature implementations such as ngtcp2, quiche, MsQuic, picoquic, or lsquic.

M38 depends on M31b provider-backed TLS evidence for handshake and stream scenarios. If no provider backend is configured, the harness must skip handshake/stream/loss scenarios and report the missing backend explicitly.

**Files:**
- Create: `tests/interop/README.md`
- Create: `tests/interop/flowq_endpoint_driver.cpp`
- Create: `tests/interop/scenarios/basic_handshake.json`
- Create: `tests/interop/scenarios/stream_echo.json`
- Create: `tests/interop/scenarios/loss_recovery.json`
- Modify: `CMakeLists.txt`
- Modify: `docs/development.md`
- Modify: `docs/basic-complete.md`

**TDD steps:**
- [ ] Add an opt-in CMake option `FLOWQ_BUILD_INTEROP` defaulting off.
- [ ] Write a harness self-test that validates scenario parsing without launching external processes.
- [ ] Write a harness self-test that verifies missing peer binaries produce a skipped result, not a false pass.
- [ ] Write a harness self-test that verifies missing provider-backed TLS adapter produces a skipped result for handshake/stream scenarios, not a false pass.
- [ ] Add scenario files for handshake, stream echo, and loss recovery with explicit expected packet/event milestones.
- [ ] Verify RED for scenario parser and skip behavior.
- [ ] Implement parser and opt-in runner shell.
- [ ] Verify GREEN and full default CTest with interop disabled.

**Acceptance gate:** Interop harness is reproducible and opt-in. FlowQ can only claim interoperability for scenarios that pass against named peer versions, named FlowQ TLS backend, backend version, and CI or release evidence.

## M39: Production Release Evidence Gate and Status Wording Review

**Goal:** Define the exact evidence required before FlowQ can change public wording from non-production baseline to production candidate.

**Files:**
- Create: `docs/production-readiness-gate.md`
- Create: `docs/release-checklist.md`
- Modify: `README.md`
- Modify: `PLAN.md`
- Modify: `docs/basic-complete.md`
- Modify: `docs/development.md`

**TDD and verification steps:**
- [ ] Add a documentation checklist requiring full CTest, package-consumer, vector tests, sanitizer/fuzz gates, interop scenarios, external crypto backend version record, and Oracle/security review.
- [ ] Add explicit language for allowed statuses: `non-production baseline`, `production-readiness milestone`, `production candidate`, and `production-ready`.
- [ ] State that `production-ready` requires all release checklist gates plus human security review outside the agent loop.
- [ ] Require production-candidate wording to state exact supported scope: client/server support, QUIC version, operating systems, TLS backend name/version, cipher suites, interop peer versions, scenarios passed, and unsupported items such as migration, stateless reset, path validation, 0-RTT, HTTP/3, or WebTransport if still deferred.
- [ ] Verify docs do not contain forbidden claims before the checklist is complete.
- [ ] Run full local verification and request Oracle review.

**Acceptance gate:** Status wording is evidence-bound. A future production claim cannot be made by completing one feature milestone, and any production-candidate claim must state its supported scope narrowly.

## Execution Policy

Implement one milestone at a time. For each milestone:

1. Write failing tests first.
2. Verify RED for the expected reason.
3. Implement minimal production code.
4. Verify focused GREEN.
5. Run full CTest, install/export, and package-consumer verification.
6. Run `git diff --check` and changed-file scan.
7. Request Oracle review before moving on.
8. Leave commits to explicit user request.

## Current Recommendation

The next executable milestone is M30. It should add structural transport-parameter encoding/decoding and config mapping without binding those parameters into TLS extensions yet.
