# M10 Stream Frame Scheduler Seam Design

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development for implementation. This stage is a pure stream-scope outbound frame scheduler; do not add connection-loop integration, packet assembly, Application Data packets, TLS, short headers, congestion control, public APIs, RESET_STREAM, STOP_SENDING, or production QUIC claims.

**Goal:** Batch selected outbound stream send states into deterministic vectors of existing `flowq::quic::frame` values.

**Architecture:** M10 extends `include/flowq/quic/stream.hpp` beside `stream_send_set`. It reuses existing M6/M7 STREAM production and M9 `STREAM_DATA_BLOCKED` queries, returning structural frames only so later packet/Application Data scheduling can consume them without coupling to TLS or packets.

**Tech Stack:** C++20 header-only stream core, existing `flowq::quic::frame` variants, Catch2 unit tests, and CMake preset `windows-msvc-vcpkg`.

---

## Scope

Create a deterministic stream-scope batch helper in `include/flowq/quic/stream.hpp`:

- `stream_frame_schedule_result` holds `std::vector<frame> frames` and `flowq::error error{}` with `ok() const noexcept`.
- `stream_send_set::pop_frames(std::span<const std::uint64_t> stream_ids, std::size_t max_frames, std::size_t max_stream_data_size)` iterates caller-selected stream IDs in order.
- For each selected stream, the scheduler first attempts existing `pop_frame(stream_id, max_stream_data_size)`.
- If a STREAM frame is available, it appends `frame{stream_frame}` to the batch.
- If no STREAM frame is available, it queries existing `blocked_frame(stream_id)` and appends `frame{stream_data_blocked_frame}` when present.
- The scheduler stops before exceeding `max_frames` and returns an empty successful batch when no selected stream can produce a frame.

M10 supports:

- Batching multiple STREAM frames into one ordered `std::vector<frame>` result.
- Caller-defined deterministic scheduling order through `std::span<const std::uint64_t>`.
- Per-frame stream-data chunking through the existing `max_stream_data_size` argument to `pop_frame()`.
- Emitting stream-level `STREAM_DATA_BLOCKED` values after pending stream data is credit blocked.
- Preserving existing retransmission behavior because `stream_send_state::pop_frame()` already prioritizes queued lost ranges before new data.
- Returning only complete structural frame values; no partial encoded bytes or packet payloads.

M10 intentionally excludes:

- Packet byte-budget accounting for encoded frame overhead.
- `MAX_DATA`, `DATA_BLOCKED`, connection-level flow-control accounting, and receive-window policy.
- Deduplicating or remembering sent `STREAM_DATA_BLOCKED` frames.
- Prioritization policy beyond caller-provided stream order.
- Packet assembly, `connection_loop` integration, Application Data packet space, short headers, TLS, AEAD/header protection, packet number allocation, ACK generation, loss timers, congestion control, RESET_STREAM, STOP_SENDING, public stream APIs, sockets, or production interop claims.

## Behavioral Rules

- `stream_ids` order is the scheduler order. Hidden map iteration is not a scheduler policy in M10.
- `max_frames == 0` returns a successful empty batch and must not mutate stream send state.
- `max_stream_data_size == 0` may still permit existing zero-length FIN behavior through `pop_frame()`, but it must not emit non-empty data.
- STREAM data is preferred over blocked signaling for a stream because actual data makes progress when credit permits it.
- `STREAM_DATA_BLOCKED.maximum_stream_data` is the current absolute stream limit, matching M9 behavior.
- Repeated scheduler calls may return repeated identical blocked frames; M10 remains stateless with respect to blocked-control retransmission.
- Absent selected streams may be materialized by `pop_frame()` only if the caller selected them. They produce no frame and no blocked signal.
- Errors from `pop_frame()` are propagated immediately with frames selected so far preserved in the result.

## RFC Semantics Preserved

- QUIC packets may contain multiple complete frames, and frames do not span packets (RFC 9000 §12.4). M10 returns complete frame values but leaves packet fitting to a later layer.
- STREAM frame boundaries are not semantic; stream offset and data identity are semantic (RFC 9000 §2.2). M10 relies on the existing stream send core to preserve offsets and bytes.
- A sender must not send STREAM data beyond peer flow-control limits (RFC 9000 §4.1). M10 delegates stream-level enforcement to existing `stream_send_state::pop_frame()`.
- `STREAM_DATA_BLOCKED` carries the stream ID and blocked absolute limit (RFC 9000 §19.13). M10 reuses M9 `blocked_frame()` values.

## TDD Plan

### Task 1: Add batch result and deterministic STREAM batching

- [ ] Add failing stream-set tests that call `pop_frames()` with selected stream IDs and assert STREAM frame variants in the same order.
- [ ] Add a failing test for `max_frames` limiting without mutating unselected later streams.
- [ ] Implement `stream_frame_schedule_result` and `stream_send_set::pop_frames()` minimally.

### Task 2: Add blocked and edge-case scheduling coverage

- [ ] Add failing tests that a blocked selected stream yields `stream_data_blocked_frame` when no STREAM data can be popped.
- [ ] Add failing tests that credit updates make later batches emit STREAM suffix data instead of a blocked frame.
- [ ] Add failing tests for empty selection, zero `max_frames`, and absent streams producing successful empty batches.
- [ ] Extend implementation only as needed to pass the tests.

### Task 3: Documentation and verification

- [ ] Update `docs/development.md` with an M10 stream frame scheduler seam section and explicit non-goals.
- [ ] Run focused stream tests and full CTest.
- [ ] Run Oracle review before committing.

## Verification

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg
ctest --preset windows-msvc-vcpkg --timeout 10
```

If `clangd` is available, run LSP diagnostics on modified headers/tests. If not, record that LSP diagnostics were unavailable and rely on compiler/test output.

## Self-Review

- Spec coverage: Covers result shape, deterministic selected-stream ordering, STREAM batching, frame count limiting, blocked-frame fallback, edge cases, documentation, verification, and explicit non-goals.
- Placeholder scan: No TBD/TODO placeholders are present; all deferred features are explicit non-goals.
- Type consistency: Uses existing `frame`, `stream_frame`, `stream_data_blocked_frame`, `stream_send_set`, `stream_send_result`, and `flowq::error` names.
