# M6 Minimal STREAM Send Design

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development for implementation. This stage is a pure send-side STREAM frame generation slice; do not add flow control, TLS, short headers, public APIs, or production QUIC claims.

**Goal:** Add a minimal send-side stream core that turns immutable application bytes into STREAM frames with stable offsets, FIN/final-size consistency, and ACK/loss callbacks for future retransmission integration.

**Architecture:** M6 extends `include/flowq/quic/stream.hpp` under `flowq::quic` with a sibling send core next to the M5a receive core. It owns per-stream send bytes and range state, emits existing `stream_frame` values, and leaves packet assembly to `connection_loop` / `packet_pipeline`.

**Tech Stack:** C++20 header-only QUIC core, existing `flowq::buffer`, `flowq::error`, `flowq::quic::stream_frame`, and Catch2 unit tests.

---

## Scope

Create send-side pieces in `include/flowq/quic/stream.hpp`:

- `stream_send_range` describes a byte range and optional FIN marker.
- `stream_send_result` returns a generated `stream_frame`, range metadata, and `flowq::error`.
- `stream_send_state` owns immutable bytes for one stream, tracks stable offsets, and emits STREAM frames.
- `stream_send_set` mirrors `stream_receive_set` for per-stream send states.

M6 supports:

- Appending bytes to a stream before FIN.
- Generating STREAM frames up to a caller-provided payload limit.
- Stable offset assignment starting at zero.
- Explicit `length_present = true` on generated STREAM frames.
- `offset_present = false` only for offset zero; non-zero offsets set it true.
- FIN final size fixed by `finish()` as the number of bytes appended.
- Empty FIN frames for empty streams or already-sent data.
- `on_lost(range)` to make an emitted range retransmittable with the same bytes/offset.
- `on_acked(range)` to suppress retransmission of acknowledged byte ranges.

M6 intentionally excludes:

- Flow-control credit accounting and `MAX_DATA` / `MAX_STREAM_DATA`.
- Packet-to-stream range mapping inside `connection_loop`.
- Congestion control, prioritization, scheduling, stream reset/stop-sending, public stream APIs, short headers, TLS, and Application Data packet support.

## Behavioral Rules

- Stream bytes are immutable once appended. Retransmission must reproduce identical bytes at identical offsets.
- `append(data)` fails after `finish()` has fixed final size.
- `finish()` is idempotent only before new bytes are appended; once called, final size is immutable.
- `pop_frame(max_data_size)` returns no frame when there is no unsent/lost data and no unsent FIN.
- `max_data_size == 0` can emit only a zero-length FIN frame; it cannot emit data.
- Data frames mark `fin = true` only when the stream is finished and that frame reaches `final_size`.
- Loss callback marks the corresponding bytes and/or FIN for retransmission unless already acknowledged.
- ACK callback marks the corresponding bytes and/or FIN as acknowledged and prevents later loss callbacks for the same information from re-queueing it.

## TDD Plan

### Task 1: Basic frame generation

- [ ] Add failing tests in `tests/unit/quic_stream_tests.cpp` for appending `"hello"` and popping a STREAM frame with stream ID, offset zero, LEN set, payload bytes, and no FIN.
- [ ] Add failing tests for fragmentation with `max_data_size = 2`, producing offsets `0`, `2`, `4`.
- [ ] Implement minimal append/pop state.

### Task 2: FIN and final size

- [ ] Add failing tests that `finish()` makes the final data frame carry FIN.
- [ ] Add failing tests for empty-stream FIN frame.
- [ ] Add failing tests that append after `finish()` returns `protocol_error`.
- [ ] Implement final-size and FIN state.

### Task 3: ACK/loss callbacks

- [ ] Add failing tests that lost data is retransmitted with the same offset and bytes.
- [ ] Add failing tests that acknowledged ranges are not retransmitted after later loss callbacks.
- [ ] Add failing tests that lost FIN is retransmitted.
- [ ] Implement range state and callbacks.

### Task 4: Send set routing

- [ ] Add failing tests that `stream_send_set` keeps offsets independent for stream IDs `0` and `4`.
- [ ] Implement keyed send state routing.

## Verification

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg
ctest --preset windows-msvc-vcpkg --timeout 10
```

If `clangd` is available, run LSP diagnostics on modified headers/tests. If not, record that LSP diagnostics were unavailable and rely on compiler/test output.

## Self-Review

- Spec coverage: Covers send offsets, frame generation, FIN final size, loss retransmission, ACK suppression, and explicit non-goals.
- Placeholder scan: No TBD/TODO placeholders are present; deferred features are explicit non-goals.
- Type consistency: API uses existing `stream_frame` and follows current FlowQ value-result style.
