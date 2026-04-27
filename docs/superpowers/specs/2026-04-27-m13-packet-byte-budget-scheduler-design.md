# M13 Packet Byte Budget Scheduler Design

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development for implementation. This stage budgets complete encoded frames only; do not implement UDP PMTU, congestion, AEAD overhead, or real packet-number-length selection.

**Goal:** Select a prefix of complete frame values whose encoded bytes fit a caller-provided packet payload budget.

**Architecture:** M13 adds a pure helper that measures candidate frames through the existing frame encoder. The connection layer can use the helper before packet assembly, while packet headers and protection overhead remain outside this budget.

**Tech Stack:** C++20, `frame.hpp`, `packet_pipeline.hpp`, `connection.hpp`, Catch2.

---

## Scope

- Encode each candidate frame to measure its size.
- Preserve candidate order.
- Stop before the first non-fitting frame.
- Return selected frames, encoded size, and remaining candidates or remaining budget according to existing result style.
- Integrate at the connection scheduling boundary after M11/M12.

## Behavioral Rules

- Frames are atomic and never partially selected.
- A frame that cannot fit in an empty budget yields an empty successful selection or an explicit too-large result, whichever tests define.
- Budget means frame payload bytes only, not UDP datagram size.

## Non-Goals

- Datagram PMTU, Initial minimum datagram sizing, padding policy.
- AEAD tag, packet header, packet-number-length, or coalescing overhead.
- Congestion/pacing decisions.

## Verification

Run packet pipeline and connection tests, then full CTest.
