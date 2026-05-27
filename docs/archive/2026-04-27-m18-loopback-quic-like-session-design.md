# M18 Loopback QUIC-Like Session Design

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development for implementation. This stage is the basic usable non-production FlowQ point; it is a deterministic loopback harness, not wire-compatible QUIC.

**Goal:** Demonstrate two FlowQ endpoints exchanging stream data, ACKing packets, recovering lost stream bytes, respecting flow control, and closing/resetting over in-memory transport.

**Architecture:** M18 builds an integration harness around existing connection loops and structural packet paths. The harness moves datagrams through memory queues, can drop/reorder packets deterministically, and observes delivered stream bytes.

**Tech Stack:** C++20, FlowQ QUIC headers, Catch2 integration tests, CMake preset `windows-msvc-vcpkg`.

---

## Scope

- Create two endpoint/connection-loop objects in tests.
- Pump outbound datagrams from one endpoint into the peer.
- Verify ordered stream delivery.
- Verify deterministic packet loss causes stream retransmission and eventual delivery.
- Verify flow-control prefix/suffix behavior.
- Verify close/reset observability.

## Behavioral Rules

- Tests are deterministic; no random timing or real sockets.
- Lost stream data must be resent as information in a new packet.
- Loopback uses test protection and must be labeled non-production.

## Implemented Coverage

- The integration harness connects two `connection_loop` instances with an in-memory datagram pump; no sockets or timers are
  owned by the harness.
- STREAM data is verified in both directions over structural Application packets.
- ACK exchange is verified by observing the sender's Application sent-packet state.
- Deterministic loss is verified by dropping the original STREAM datagram, acknowledging a later Application packet, expiring
  the recovery seam, and observing retransmitted bytes at the peer.
- Flow-control prefix/suffix delivery is verified with initial stream credit and a later `MAX_STREAM_DATA` update.
- Reset observability is verified with Application `RESET_STREAM` and peer receive-stream reset state.

## Non-Goals

- Real UDP client/server APIs.
- Real TLS, AEAD, header protection, address validation, migration, congestion, HTTP/3.
- Production interoperability.

## Verification

Run integration tests and full CTest. M18 is the planned “basic usable” report point.
