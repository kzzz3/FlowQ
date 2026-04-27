# Post-M10 FlowQ Roadmap

> **For agentic workers:** Use this roadmap before selecting the next implementation plan. It is an ordering and scope document, not implementation code.

**Goal:** Define the baseline post-M10 documentation and implementation sequence needed to reach a basic usable, non-production QUIC-like FlowQ loopback.

**Architecture:** Continue the existing staged approach: pure values first, deterministic unit tests second, connection integration third, loopback proof last. Real TLS/AEAD/header protection stays behind an adapter seam and must not be handwritten as production crypto.

**Tech Stack:** C++20, standalone Asio boundary, Catch2, existing FlowQ QUIC headers, CMake preset `windows-msvc-vcpkg`.

---

## Completed Base

- M0-M10 established build/test infrastructure, ASIO senders, protocol action seams, QUIC varint/frame/header codecs, ACK/loss/recovery primitives, long-packet pipeline, minimal Initial/Handshake connection loop, STREAM receive/send cores, stream flow-control signals, and outbound stream frame scheduling.

## Post-M10 Milestones

1. **M11 Connection Stream Integration**: route decoded STREAM frames into `stream_receive_set` and expose connection-owned outbound stream scheduling wrappers.
2. **M12 Connection-Level Flow Control**: add aggregate connection byte-credit accounting and `MAX_DATA` / `DATA_BLOCKED` handling.
3. **M13 Packet Byte Budget Scheduler**: select complete encoded frames that fit a payload budget before packet assembly.
4. **M14 Recovery Timer Integration**: expose deterministic recovery timer deadlines and timer-expiry loss results from connection state.
5. **M15 Stream ACK/Loss Mapping**: track STREAM ranges per packet and call stream ACK/loss callbacks from packet outcomes.
6. **M16 Application Data Structural Packet Space**: add explicit non-production Application packet path for tests and loopback.
7. **M17 Minimal Close and Stream Reset Codec**: add `RESET_STREAM` / `STOP_SENDING` codecs and minimal stream effects.
8. **M18 Loopback QUIC-Like Session**: prove stream send/receive, ACK/loss, retransmission, flow control, and close/reset across two in-memory endpoints.
9. **M19 Crypto Adapter Seam**: make test protection explicit and define the external TLS/packet-protection adapter boundary.

## Stop and Report Point

M18 is the practical “basic usable FlowQ” point: deterministic loopback sessions can exchange stream data, acknowledge packets, recover lost stream bytes, respect flow control, and close/reset without claiming production QUIC.

M19 is a safety/documentation boundary after that point. It prepares for real security integration but does not make FlowQ interoperable or secure by itself.

## Explicitly Deferred Beyond M19

- Real TLS 1.3 handshake integration.
- AEAD and header protection backed by a mature external library.
- Production short-header packet number reconstruction.
- UDP listener/client public APIs.
- Congestion control, pacing, ECN, migration, Retry integrity, address validation, 0-RTT, HTTP/3, WebTransport, multipath, and production interoperability.

## Documentation Matrix

- Design docs live in `docs/superpowers/specs/2026-04-27-mXX-*-design.md`.
- Execution plans live in `docs/superpowers/plans/2026-04-27-mXX-*.md`.
- `docs/development.md` is updated after each milestone is implemented and verified.

## Baseline Documentation Policy

The M11-M19 documents are baseline plans, not frozen specifications. During each future milestone:

- update that milestone's design document before changing code if exploration changes the scope;
- update its implementation plan when tests reveal a better task split;
- update `docs/development.md` after implementation and verification;
- keep deferred production QUIC items explicit instead of silently expanding the milestone;
- prefer small doc revisions in the same stage over trying to predict every detail upfront.
