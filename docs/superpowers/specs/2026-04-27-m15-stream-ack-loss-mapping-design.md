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
- Cover both ACK-threshold packet loss and recovery-timer time-threshold packet loss.

## Behavioral Rules

- QUIC retransmits information, not packet numbers.
- ACKed ranges suppress later retransmission.
- Lost FIN is represented as a stream send range with `fin=true`.
- Packet budget splitting records only STREAM frames actually selected into a sent packet.
- Retransmissions are not fresh stream data for connection-level flow-control accounting and do not increment aggregate sent
  data a second time.
- Stream send state accepts ACK/loss ranges only for bytes already emitted by the scheduler. Late ACKs for lost FIN ranges
  suppress FIN retransmission, including after partial retransmission splits the lost data range, but invalid unsent FIN ACKs
  and loss signals remain ignored.
- Manually queued STREAM frames outside `stream_send_set` can be observed in the sent ledger, but ACK/loss routing must not
  create send state for them or mutate an existing unsent send state.

## Non-Goals

- Congestion, pacing, priority scheduling, RESET_STREAM, STOP_SENDING.
- Full recovery policy beyond existing packet outcome signals.

## Verification

Run connection and stream tests, then full CTest.
