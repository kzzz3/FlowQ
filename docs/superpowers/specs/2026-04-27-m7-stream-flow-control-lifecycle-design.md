# M7 Stream Flow-Control and Lifecycle Core Design

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development for implementation. This stage is a pure stream-core slice; do not add wire flow-control frame codecs, connection-loop integration, TLS, short headers, public APIs, or production QUIC claims.

**Goal:** Add minimal stream-level flow-control accounting and explicit FIN lifecycle inspectors to the existing STREAM receive/send core.

**Architecture:** M7 extends `include/flowq/quic/stream.hpp` without creating a new transport layer. Receive state enforces a local stream byte limit before buffering data. Send state enforces the peer stream byte limit before emitting new STREAM data while still allowing retransmission of already-sent ranges. Lifecycle remains FIN-based and queryable through compact inspectors.

**Tech Stack:** C++20 header-only QUIC core, existing `flowq::buffer`, `flowq::error`, `flowq::quic::stream_frame`, and Catch2 unit tests.

---

## Scope

Create stream-core additions in `include/flowq/quic/stream.hpp`:

- `detail::stream_flow_control_error` returns `flowq::error_code::flow_control_error`.
- `stream_receive_state` accepts an optional initial stream receive limit and rejects incoming STREAM bytes beyond that limit.
- `stream_receive_state::update_max_data(limit)` monotonically raises the receive limit for later frames.
- `stream_send_state` accepts an optional initial peer stream limit and gates new unsent bytes by that limit.
- `stream_send_state::update_max_data(limit)` monotonically raises peer credit and ignores smaller stale updates.
- `stream_send_state` exposes FIN lifecycle inspectors: `finished()`, `fin_sent()`, `fin_acked()`, `closed()`, and `blocked()`.
- `stream_receive_set` and `stream_send_set` expose small routing helpers for per-stream credit updates.

M7 supports:

- Accepting receive data whose end offset is exactly at the stream receive limit.
- Rejecting receive data whose end offset exceeds the stream receive limit with `flow_control_error`.
- Raising receive credit and then accepting previously blocked data.
- Limiting new send data by peer stream credit and payload size.
- Returning no frame, rather than an error, when unsent data exists but stream credit is exhausted.
- Raising peer stream credit and then emitting pending bytes with stable offsets.
- Delaying FIN until all data up to the final size has been credit-authorized and emitted.
- Retransmitting lost already-sent ranges without rechecking current unsent credit.
- Reporting send closure only after FIN has been acknowledged.

M7 intentionally excludes:

- Wire codecs for `MAX_DATA`, `MAX_STREAM_DATA`, `DATA_BLOCKED`, `STREAM_DATA_BLOCKED`, `RESET_STREAM`, or `STOP_SENDING`.
- Connection-level flow-control enforcement.
- Packet-to-stream ACK/loss mapping inside `connection_loop`.
- Application Data packet support, short headers, TLS, AEAD/header protection, congestion control, stream scheduling, prioritization, reset/stop-sending, and public stream APIs.

## Behavioral Rules

- Stream limits are byte offsets: a frame with `offset + data.size() == max_data` is allowed.
- STREAM offset overflow remains a `protocol_error`; exceeding a non-overflowing stream credit limit is a `flow_control_error`.
- Receive credit updates are monotonic. Smaller limits are ignored because RFC flow-control limits do not shrink.
- Send credit updates are monotonic. Smaller stale peer limits are ignored.
- New STREAM data is constrained by both caller payload limit and peer stream credit.
- Lost ranges represent information already emitted earlier and are retransmitted independently of current new-data credit.
- `max_data_size == 0` still cannot emit data; it can emit only a zero-length FIN when the final size is already within credit.
- `blocked()` means unsent bytes are buffered and stream credit prevents emitting the next byte.
- `closed()` on send state means the application finished the stream and the FIN has been acknowledged.

## TDD Plan

### Task 1: Receive stream credit

- [ ] Add failing tests for receive data ending exactly at credit and data exceeding credit.
- [ ] Implement receive limit storage, monotonic update, and `flow_control_error` failure.
- [ ] Add a failing test that a credit increase permits a previously blocked offset.

### Task 2: Send stream credit

- [ ] Add failing tests that send output is clipped by stream credit and then blocks.
- [ ] Implement peer credit storage, monotonic update, and `blocked()`.
- [ ] Add a failing test that a later credit increase emits pending data at stable offsets.

### Task 3: FIN lifecycle and retransmission boundaries

- [ ] Add failing tests that FIN is delayed until final bytes are credit-authorized.
- [ ] Add failing tests for `finished()`, `fin_sent()`, `fin_acked()`, and `closed()`.
- [ ] Add a failing test that lost already-sent data retransmits even when new data is blocked.
- [ ] Implement lifecycle inspectors and preserve loss/ACK behavior.

### Task 4: Set routing

- [ ] Add failing tests for receive/send set credit update routing.
- [ ] Implement minimal routing helpers.

## Verification

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg
ctest --preset windows-msvc-vcpkg --timeout 10
```

If `clangd` is available, run LSP diagnostics on modified headers/tests. If not, record that LSP diagnostics were unavailable and rely on compiler/test output.

## Self-Review

- Spec coverage: Covers receive credit, send credit, monotonic credit updates, FIN lifecycle inspectors, retransmission boundaries, set routing, and explicit non-goals.
- Placeholder scan: No TBD/TODO placeholders are present; deferred features are explicit non-goals.
- Type consistency: API follows existing `stream_receive_state`, `stream_send_state`, `stream_receive_set`, and `stream_send_set` naming/result conventions.
