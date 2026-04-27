# M32 Short Header Shell Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add RFC-shaped short-header value types and parser/encoder shell using M27 packet-number helpers.

**Architecture:** Model short-header fields and protected payload boundaries. Header protection removal and 1-RTT AEAD stay behind M28/M31 boundaries.

**Tech Stack:** C++20 header codec, Catch2, existing `packet_header.hpp` and `packet_pipeline.hpp`.

---

## Files

- Modify: `include/flowq/quic/packet_header.hpp`
- Modify: `include/flowq/quic/packet_pipeline.hpp`
- Modify: `tests/unit/quic_packet_header_tests.cpp`
- Modify: `tests/unit/quic_packet_pipeline_tests.cpp`
- Modify: `docs/development.md`
- Modify: `PLAN.md`

## Tasks

- [ ] Write RED tests for short-header fixed bit, spin bit preservation, key phase, destination connection ID, packet-number length, and protected payload bytes.
- [ ] Write RED tests proving parser requires caller-provided destination connection ID length.
- [ ] Write RED tests proving production-required parsing fails closed without header-protection context.
- [ ] Verify RED.
- [ ] Add `short_header` value type and decode/encode shell.
- [ ] Use M27 helpers for packet-number length and truncated packet-number bytes.
- [ ] Keep structural Application envelope separate from real short headers.
- [ ] Verify GREEN with packet-header and packet-pipeline focused tests.
- [ ] Run full verification and Oracle review.

## Acceptance Gate

Short-header structure exists for future protection integration, but FlowQ still does not claim 1-RTT interoperability.
