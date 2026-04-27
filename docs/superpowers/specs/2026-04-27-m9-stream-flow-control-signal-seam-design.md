# M9 Stream Flow-Control Signal Seam Design

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development for implementation. This stage is a pure stream-scope signal seam; do not add connection-loop integration, connection-level flow-control policy, Application Data, TLS, short headers, public APIs, or production QUIC claims.

**Goal:** Bridge M7 stream send credit and M8 flow-control frame values by applying `MAX_STREAM_DATA` to stream send state and exposing `STREAM_DATA_BLOCKED` when a stream is blocked.

**Architecture:** M9 extends `include/flowq/quic/stream.hpp` with tiny helpers on `stream_send_state` and `stream_send_set`. The seam consumes existing `max_stream_data_frame` values and produces existing `stream_data_blocked_frame` values; it does not schedule frames, mutate packet state, or own connection policy.

**Tech Stack:** C++20 header-only stream core, existing M7 stream credit state, existing M8 flow-control frame structs, and Catch2 unit tests.

---

## Scope

Create stream-scope helpers in `include/flowq/quic/stream.hpp`:

- `stream_send_state::update_max_data(const max_stream_data_frame&)` applies the frame when its `stream_id` matches the state stream ID and ignores mismatched frames.
- `stream_send_set::update_max_data(const max_stream_data_frame&)` routes the frame by `stream_id` and applies `maximum_stream_data` monotonically.
- `stream_send_state::blocked_frame() const` returns `std::optional<stream_data_blocked_frame>` with `{stream_id, current_stream_limit}` when local unsent data is blocked by stream credit.
- `stream_send_set::blocked_frame(stream_id) const` reports blocked state for an existing selected stream without creating new stream state.

M9 supports:

- Raising stream send credit from `MAX_STREAM_DATA` frame values.
- Ignoring stale/lower `MAX_STREAM_DATA` through the existing monotonic credit behavior.
- Keeping mismatched single-state `MAX_STREAM_DATA` frames inert.
- Producing `STREAM_DATA_BLOCKED` frame values only when unsent data is blocked by stream-level credit.
- Reporting no blocked frame for absent streams, empty streams, unblocked streams, or streams whose data is fully emitted.

M9 intentionally excludes:

- Applying `MAX_DATA` / `DATA_BLOCKED`; those are connection-level signals.
- Automatically scheduling, retransmitting, or deduplicating blocked frames.
- Receiving peer `STREAM_DATA_BLOCKED` as connection policy.
- Connection-level flow-control accounting, `connection_loop` integration, packet-type legality, Application Data packet handling, short headers, TLS, AEAD/header protection, congestion control, RESET_STREAM, STOP_SENDING, stream scheduling, and public APIs.

## Behavioral Rules

- `MAX_STREAM_DATA.maximum_stream_data` is an absolute byte-offset limit, not an increment.
- Applying a lower/equal `MAX_STREAM_DATA` must not shrink peer credit.
- `blocked_frame()` reports the current stream limit that blocks progress, matching RFC 9000 `STREAM_DATA_BLOCKED` semantics.
- `blocked_frame()` is a query only; it does not mark the frame sent and does not suppress future identical queries.
- `pop_frame()` remains the only producer of STREAM data; M9 does not change send scheduling.
- Lost data retransmission and FIN credit behavior from M7 remain unchanged.

## TDD Plan

### Task 1: Apply `MAX_STREAM_DATA` frame values

- [ ] Add failing tests that a `max_stream_data_frame` unblocks pending stream data.
- [ ] Add failing tests that stale frame limits do not shrink credit and mismatched stream IDs do not mutate a single state.
- [ ] Implement frame overloads for `stream_send_state` and `stream_send_set`.

### Task 2: Report `STREAM_DATA_BLOCKED`

- [ ] Add failing tests that a blocked stream produces `stream_data_blocked_frame{stream_id, limit}`.
- [ ] Add failing tests that unblocked, empty, fully emitted, and absent streams report no blocked frame.
- [ ] Implement `blocked_frame()` helpers.

### Task 3: Documentation and verification

- [ ] Update `docs/development.md` with an M9 signal-seam scope and explicit non-goals.
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

- Spec coverage: Covers `MAX_STREAM_DATA` application, stale/mismatch behavior, `STREAM_DATA_BLOCKED` generation, set routing, and explicit non-goals.
- Placeholder scan: No TBD/TODO placeholders are present; deferred connection policies are explicit non-goals.
- Type consistency: Uses existing `max_stream_data_frame`, `stream_data_blocked_frame`, `stream_send_state`, and `stream_send_set` naming.
