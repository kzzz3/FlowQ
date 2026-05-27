# M35 Routing Version Retry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add deterministic connection ID routing, Version Negotiation decisions, Retry preparation, and address-validation interfaces.

**Architecture:** Keep server listener mechanics out of this milestone. Build pure routing and decision helpers that production endpoints can use later.

**Tech Stack:** C++20 value types, Catch2, packet header and connection integration.

---

## Files

- Create: `include/flowq/quic/connection_routing.hpp`
- Create: `tests/unit/quic_connection_routing_tests.cpp`
- Modify: `include/flowq/quic/packet_header.hpp`
- Modify: `include/flowq/quic/connection.hpp`
- Modify: `tests/unit/quic_packet_header_tests.cpp`
- Modify: `tests/unit/quic_connection_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/development.md`

## Tasks

- [ ] Write RED tests for destination connection ID lookup, unknown connection ID handling, and connection ID retirement bookkeeping.
- [ ] Write RED tests for selecting Version Negotiation packets from supported versions.
- [ ] Write RED tests for Retry token validation interface shape.
- [ ] Write RED tests proving Retry integrity validation is delegated to crypto provider capabilities.
- [ ] Verify RED.
- [ ] Implement routing table values, version negotiation helpers, and Retry decision interfaces.
- [ ] Verify GREEN, full CTest, install, and package-consumer.

## Acceptance Gate

Routing and Retry preparation is deterministic. FlowQ still does not claim a production server listener.
