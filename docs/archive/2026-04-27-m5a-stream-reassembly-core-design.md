# M5a STREAM Reassembly Core Design

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development for implementation. This stage is a pure STREAM state/reassembly slice; do not add ASIO, TLS, short headers, flow control signaling, retransmission queues, or public API claims.

**Goal:** Convert decoded QUIC STREAM frames into ordered byte ranges with deterministic duplicate, overlap, gap, and FIN/final-size handling.

**Architecture:** M5a adds an independent stream core in `include/flowq/quic/stream.hpp` under `flowq::quic`, above the structural frame codec and below future connection/application APIs. It consumes existing `stream_frame` values, tracks receive state per stream ID, buffers out-of-order data by offset, and exposes contiguous bytes through value results.

**Tech Stack:** C++20 header-only QUIC core, existing `flowq::buffer`, `flowq::error`, and `flowq::quic::stream_frame`.

---

## Scope

Create `include/flowq/quic/stream.hpp` with:

- `stream_id_info` and `classify_stream_id(std::uint64_t)` for client/server initiator and bidirectional/unidirectional bits.
- `stream_receive_result` containing delivered ordered bytes, final-size state, close state, and `flowq::error`.
- `stream_receive_state` for one stream's receive-side reassembly.
- `stream_receive_set` for routing STREAM frames to per-stream receive states and returning deliveries tagged by stream ID.

M5a supports:

- Receiving `stream_frame` data at arbitrary offsets.
- Buffering gaps without delivering bytes past the missing prefix.
- Delivering only newly contiguous ordered bytes.
- Ignoring exact duplicate bytes already delivered or buffered.
- Accepting overlapping frames when overlapping bytes are identical.
- Rejecting overlapping frames when the same offset carries different bytes.
- Tracking final size when FIN is observed: `offset + data.size()`.
- Rejecting inconsistent final sizes and bytes beyond final size.

M5a intentionally excludes:

- Flow-control windows and `MAX_DATA` / `MAX_STREAM_DATA` frame generation.
- `RESET_STREAM`, `STOP_SENDING`, `STREAMS_BLOCKED`, `MAX_STREAMS`, stream scheduling, priorities, retransmission, send buffering, and application API.
- Packet-number-space or encryption-level changes; M4b still rejects Application Data packets.
- Any promise of full RFC-compliant QUIC stream lifecycle.

## File Structure

- Create `include/flowq/quic/stream.hpp`
  - Owns stream ID classification, receive reassembly, final-size validation, and per-stream routing.
- Create `tests/unit/quic_stream_tests.cpp`
  - Covers ordered delivery, gaps, duplicate suppression, identical overlaps, conflicting overlaps, FIN final size, and stream ID classification.
- Modify `tests/CMakeLists.txt`
  - Registers `unit/quic_stream_tests.cpp`.
- Modify `docs/development.md`
  - Documents M5a STREAM receive/reassembly scope and non-goals.

## Public API Shape

```cpp
namespace flowq::quic {

enum class stream_initiator { client, server };
enum class stream_direction { bidirectional, unidirectional };

struct stream_id_info {
    std::uint64_t id{};
    stream_initiator initiator{};
    stream_direction direction{};
};

struct stream_receive_result {
    flowq::buffer data;
    bool final_size_known{};
    std::uint64_t final_size{};
    bool closed{};
    flowq::error error{};
    [[nodiscard]] bool ok() const noexcept;
};

class stream_receive_state {
public:
    [[nodiscard]] stream_receive_result receive(const stream_frame& frame);
    [[nodiscard]] std::uint64_t next_offset() const noexcept;
    [[nodiscard]] bool final_size_known() const noexcept;
    [[nodiscard]] std::uint64_t final_size() const noexcept;
    [[nodiscard]] bool closed() const noexcept;
};

struct stream_delivery {
    std::uint64_t stream_id{};
    stream_receive_result result;
};

class stream_receive_set {
public:
    [[nodiscard]] stream_delivery receive(const stream_frame& frame);
    [[nodiscard]] const stream_receive_state* find(std::uint64_t stream_id) const noexcept;
};

}
```

## Behavioral Rules

- `classify_stream_id(0)` is client-initiated bidirectional; `1` is server-initiated bidirectional; `2` is client-initiated unidirectional; `3` is server-initiated unidirectional.
- `stream_receive_state::receive(frame)` treats `frame.offset` as zero when `frame.offset_present == false`, matching the existing structural codec.
- Data before `next_offset()` is duplicate/overlap. Identical bytes are accepted and ignored; differing bytes return `protocol_error`.
- Data after `next_offset()` is buffered until all earlier offsets are present.
- A receive call may deliver zero bytes when it only fills a future gap or carries duplicate data.
- FIN fixes final size to `frame.offset + frame.data.size()`. A later FIN with a different final size returns `protocol_error`.
- Any data range extending beyond the known final size returns `protocol_error`.
- The stream is `closed()` once final size is known and `next_offset() == final_size()`.
- M5a buffers bytes in `std::map<std::uint64_t, flowq::buffer>` or equivalent ordered structure. No memory cap is implemented yet; flow-control and buffering limits are M5b.

## TDD Plan

### Task 1: Ordered and out-of-order delivery

- [ ] Add `tests/unit/quic_stream_tests.cpp` and register it in `tests/CMakeLists.txt`.
- [ ] Write a failing test that `receive(offset=0, "he")` delivers `"he"` and advances `next_offset()` to 2.
- [ ] Write a failing test that `receive(offset=2, "llo")` before offset 0 delivers no bytes, then offset 0 delivers `"hello"`.
- [ ] Implement minimal buffering and contiguous drain.

### Task 2: Duplicate and overlap handling

- [ ] Add failing tests for exact duplicate data producing no new delivery.
- [ ] Add failing tests for identical overlap producing only the new suffix.
- [ ] Add failing tests for conflicting overlap returning `protocol_error`.
- [ ] Implement overlap validation before buffering.

### Task 3: FIN and final size

- [ ] Add failing tests for FIN marking final size and closed state.
- [ ] Add failing tests for inconsistent FIN final size.
- [ ] Add failing tests for data beyond known final size.
- [ ] Implement final-size tracking and validation.

### Task 4: Stream ID classification and receive set

- [ ] Add failing tests for stream ID initiator/direction bits.
- [ ] Add failing tests that `stream_receive_set` routes frames for separate stream IDs independently.
- [ ] Implement stream ID helpers and receive set.

## Verification

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg
ctest --preset windows-msvc-vcpkg --timeout 10
```

If `clangd` is available, run LSP diagnostics on modified headers/tests. If not, record that LSP diagnostics were unavailable and rely on compiler/test output.

## Self-Review

- Spec coverage: Covers RFC-essential stream ID bits, ordered delivery, gaps, duplicates, overlaps, and FIN/final-size handling.
- Placeholder scan: No TBD/TODO placeholders are present; deferred features are explicit non-goals.
- Type consistency: API names use existing FlowQ naming style and consume the existing `stream_frame` type instead of adding a parallel codec.
