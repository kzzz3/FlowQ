# M14 Recovery Timer Integration Design

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development for implementation. This stage wires existing recovery calculations into connection state as values; it does not own real timers or implement production congestion control.

**Goal:** Let `connection_loop` expose recovery timer deadlines and process timer expiry into deterministic packet-level loss results.

**Architecture:** M14 reuses M3b recovery helpers and connection sent-packet metadata. It returns deadlines/actions for external integration code to arm, keeping ASIO timer ownership outside the protocol core.

**Tech Stack:** C++20 chrono, `ack_loss.hpp`, `connection.hpp`, `core.hpp`, Catch2.

---

## Scope

- Record send times and ack-eliciting metadata needed by recovery helpers.
- Expose `next_recovery_timer(now)` or equivalent value query.
- Process a timer-expired input into packet-level loss results.
- Preserve existing ACK-driven sent-packet tracker behavior.

## Behavioral Rules

- No timer query may mutate send timestamps just because it is polled.
- ACKed packets do not keep recovery timers alive.
- Application PTO remains gated by handshake confirmation rules already modeled in M3b.

## Non-Goals

- Owning ASIO timers.
- Building PTO probe packets.
- Persistent congestion, pacing, congestion window, ECN.
- Full RFC 9002 production recovery.

## Verification

Run connection recovery tests, ACK/loss tests, and full CTest.
