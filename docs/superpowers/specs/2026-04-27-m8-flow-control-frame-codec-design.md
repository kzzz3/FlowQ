# M8 Flow-Control Frame Codec Design

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development for implementation. This stage adds structural wire codecs only; do not add connection policy, stream-state mutation, Application Data, TLS, short headers, public APIs, or production QUIC claims.

**Goal:** Add structural encode/decode support for QUIC byte-credit flow-control frames: `MAX_DATA`, `MAX_STREAM_DATA`, `DATA_BLOCKED`, and `STREAM_DATA_BLOCKED`.

**Architecture:** M8 extends `include/flowq/quic/frame.hpp` beside the existing PADDING/PING/ACK/CRYPTO/STREAM/CONNECTION_CLOSE frame codec. New frame structs become inert value types in the `flowq::quic::frame` variant, with encode overloads and decode branches that preserve RFC 9000 varint field order.

**Tech Stack:** C++20 header-only frame codec, existing QUIC varint helpers, `flowq::buffer`, `flowq::error`, and Catch2 unit tests.

---

## Scope

Create structural frame support in `include/flowq/quic/frame.hpp`:

- `max_data_frame { std::uint64_t maximum_data; }` for type `0x10`.
- `max_stream_data_frame { std::uint64_t stream_id; std::uint64_t maximum_stream_data; }` for type `0x11`.
- `data_blocked_frame { std::uint64_t maximum_data; }` for type `0x14`.
- `stream_data_blocked_frame { std::uint64_t stream_id; std::uint64_t maximum_stream_data; }` for type `0x15`.

M8 supports:

- Encoding each new frame as RFC 9000 varints in exact field order.
- Decoding each new frame from concatenated frame payloads.
- Mixing the new frame values with existing PADDING, PING, CRYPTO, STREAM, ACK, and CONNECTION_CLOSE values.
- Returning `protocol_error` through `codec_error(...)` for missing or truncated varint fields.
- Keeping unsupported frame types unsupported unless explicitly implemented.

M8 intentionally excludes:

- `MAX_STREAMS` and `STREAMS_BLOCKED`; stream-count limits need a separate stream-opening policy milestone.
- Applying decoded `MAX_STREAM_DATA` to `stream_send_state` automatically.
- Emitting `DATA_BLOCKED` or `STREAM_DATA_BLOCKED` based on M7 blocked state.
- Connection-level flow-control accounting, packet-type legality, `connection_loop` integration, Application Data packet handling, short headers, TLS, AEAD/header protection, congestion control, scheduling, RESET_STREAM, STOP_SENDING, and public APIs.

## Behavioral Rules

- All frame fields are QUIC variable-length integers.
- `MAX_DATA` and `DATA_BLOCKED` decode one field after the type: `Maximum Data`.
- `MAX_STREAM_DATA` and `STREAM_DATA_BLOCKED` decode two fields after the type: `Stream ID`, then stream byte limit.
- A frame ending before all required fields are decoded is malformed and returns a codec error.
- The codec is structural only: it does not validate whether a value increases previous credit, whether a stream exists, or whether the sender is actually blocked.
- `0x12`, `0x13`, `0x16`, and `0x17` remain unsupported in M8.

## TDD Plan

### Task 1: Positive round trips

- [ ] Add failing tests in `tests/unit/quic_frame_tests.cpp` for `MAX_DATA`, `MAX_STREAM_DATA`, `DATA_BLOCKED`, and `STREAM_DATA_BLOCKED` round trips.
- [ ] Implement value structs, variant entries, encode overloads, and decode branches.

### Task 2: Mixed payloads

- [ ] Add a failing test that decodes existing PING/CRYPTO/STREAM frames mixed with the new flow-control frames.
- [ ] Ensure decoder advances exactly by each new frame's varint fields.

### Task 3: Truncation and unsupported stream-count frame boundaries

- [ ] Add failing tests for truncated fields in each new frame.
- [ ] Add a test that stream-count flow-control frame types remain unsupported in M8.
- [ ] Keep errors structural and avoid policy checks.

## Verification

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg
ctest --preset windows-msvc-vcpkg --timeout 10
```

If `clangd` is available, run LSP diagnostics on modified headers/tests. If not, record that LSP diagnostics were unavailable and rely on compiler/test output.

## Self-Review

- Spec coverage: Covers all four M8 byte-credit frame types, field order, mixed decode, malformed input, and explicit non-goals.
- Placeholder scan: No TBD/TODO placeholders are present; deferred frame families and policies are explicit non-goals.
- Type consistency: Names follow current `*_frame` value-type style and RFC field names.
