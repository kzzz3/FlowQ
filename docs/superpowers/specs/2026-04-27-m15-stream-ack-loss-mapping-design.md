# M15 Stream ACK/Loss Mapping Design

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development for implementation. This stage maps packet outcomes to stream send ranges; it does not retransmit packets or add congestion policy.

**Goal:** Track which STREAM ranges were carried by sent packets and call stream ACK/loss callbacks when packet outcomes are known.

**Architecture:** M15 adds a connection-level packet-to-stream range ledger. Packet ACK calls `on_acked()` for carried ranges; packet loss calls `on_lost()` so future scheduling retransmits the same stream information with new packet numbers.

**Tech Stack:** C++20, `connection.hpp`, `stream.hpp`, ACK/loss trackers, Catch2.

---

## Scope

- Record stream ID, offset, length, and FIN for each STREAM frame placed in a sent packet.
- Expose test-visible ledger snapshots.
- Route ACKed ranges to stream send state.
- Route lost ranges to stream send state.
- Prove lost stream bytes are rescheduled with identical offset/data.

## Behavioral Rules

- QUIC retransmits information, not packet numbers.
- ACKed ranges suppress later retransmission.
- Lost FIN is represented as a stream send range with `fin=true`.

## Non-Goals

- Congestion, pacing, priority scheduling, RESET_STREAM, STOP_SENDING.
- Full recovery policy beyond existing packet outcome signals.

## Verification

Run connection and stream tests, then full CTest.
