# M4b Minimal Connection Loop Design

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development for implementation. This stage is a pure protocol-core slice; do not add ASIO, TLS, real AEAD, stream state, or public API claims.

**Goal:** Build a small single-connection loop that turns queued Initial/Handshake frames into outbound datagrams and turns inbound long-header datagrams back into received frames plus ACK state.

**Architecture:** M4b composes the existing packet pipeline, ACK trackers, and protocol-core action seam. The loop owns connection identifiers, per-space send packet numbers, per-space received-packet trackers, and an action queue; callers still own sockets, timers, TLS, and application-level stream semantics.

**Tech Stack:** C++20 header-only QUIC core, existing `flowq::buffer`, `flowq::endpoint`, `flowq::quic::frame`, `flowq::quic::packet_pipeline`, and `flowq::quic::ack_loss`.

---

## Scope

M4b introduces `flowq::quic::connection_loop` in `include/flowq/quic/connection.hpp`.

It supports:

- Creating a client or server side value object from local/remote connection IDs, a peer endpoint, packet protectors, and packet-pipeline config.
- Queueing Initial or Handshake frames into the relevant packet-number space.
- Flushing queued frames into `outbound_datagram` actions via `assemble_long_packet`.
- Parsing inbound Initial or Handshake datagrams via `parse_long_packet`.
- Recording received packet numbers in per-space `received_packet_tracker` instances.
- Emitting received packet metadata and decoded frames as pure actions.
- Emitting an ACK-only packet for a space that has received packets.
- Applying inbound ACK frames to per-space `sent_packet_tracker` instances.

It intentionally does not support:

- Real TLS handshake state, AEAD, header protection, key updates, or key discard.
- Short headers, 1-RTT Application Data, packet-number reconstruction, migration, or Retry validation.
- Stream state machines, stream reassembly, retransmission queues, flow control, congestion control, pacing, or timer ownership.
- Multi-connection listener/demux logic or ASIO event-loop integration.

## File Structure

- Create `include/flowq/quic/connection.hpp`
  - Defines `connection_role`, `connection_loop_config`, `received_packet_event`, `connection_loop_action`, and `connection_loop`.
  - Keeps the loop header-only like the rest of FlowQ's current protocol primitives.
- Create `tests/unit/quic_connection_tests.cpp`
  - Covers outgoing Initial flush, inbound parsing/ACK tracking, ACK-only generation, ACK frame application to sent trackers, and invalid datagram close action.
- Modify `tests/CMakeLists.txt`
  - Registers `unit/quic_connection_tests.cpp`.
- Modify `docs/development.md`
  - Documents M4b scope and non-goals.

## Public API Shape

```cpp
namespace flowq::quic {

enum class connection_role { client, server };

struct connection_loop_config {
    connection_role role{};
    std::uint32_t version{1};
    connection_id local_connection_id;
    connection_id remote_connection_id;
    flowq::endpoint peer;
    const packet_protector* initial_protector{};
    const packet_protector* handshake_protector{};
    packet_pipeline_config pipeline{};
};

struct received_packet_event {
    packet_number number{};
    std::vector<frame> frames;
    flowq::endpoint peer;
};

using connection_loop_action = std::variant<outbound_datagram, received_packet_event, close_action>;

class connection_loop {
public:
    explicit connection_loop(connection_loop_config config);

    void queue_initial(std::vector<frame> frames);
    void queue_handshake(std::vector<frame> frames);
    void flush();
    void on_datagram(inbound_datagram datagram);
    void acknowledge(packet_number_space space);

    [[nodiscard]] bool has_actions() const noexcept;
    [[nodiscard]] std::vector<connection_loop_action> drain_actions();
    [[nodiscard]] const sent_packet_tracker& sent_packets(packet_number_space space) const noexcept;
};

}
```

## Behavioral Rules

- Outgoing Initial packets use destination CID = remote CID and source CID = local CID.
- Outgoing Handshake packets use the same M4b CID mapping; future connection-ID lifecycle work may refine this.
- Initial and Handshake packet numbers are independent and start at zero.
- `queue_initial` and `queue_handshake` preserve caller-provided frame order within a packet.
- `flush` emits one packet per non-empty queue, records sent packet metadata, and clears only queues that assembled successfully.
- Packets are ack-eliciting if they contain at least one frame other than `padding_frame` and `ack_frame`.
- `on_datagram` parses only long-header Initial/Handshake packets through the matching protector.
- Successfully parsed packets are recorded in the matching received tracker and emitted as `received_packet_event`.
- Inbound ACK frames are applied to the matching sent tracker for the parsed packet-number space.
- `acknowledge(space)` emits one ACK-only packet for the requested Initial or Handshake space if that received tracker is non-empty.
- Parse or assembly failures emit `close_action` with the returned protocol error and do not throw.

## TDD Plan

### Task 1: Outbound Initial flush

- [ ] Add a failing test that queues one Initial `PING`, flushes, and expects one outbound datagram to the configured peer.
- [ ] Verify it fails because `connection.hpp` / `connection_loop` does not exist.
- [ ] Implement the minimum config, action queue, Initial frame queue, packet-number counter, and `flush` path.
- [ ] Verify the test passes.

### Task 2: Inbound receive and ACK generation

- [ ] Add a failing test that feeds the outbound Initial datagram into a peer loop and expects a `received_packet_event` with packet number zero and a `PING` frame.
- [ ] Add a failing test that calls `acknowledge(initial)` after receiving and expects an ACK-only outbound Initial datagram.
- [ ] Implement parsing, received trackers, and ACK packet generation.
- [ ] Verify tests pass.

### Task 3: Sent ACK application

- [ ] Add a failing test where one loop sends Initial packet zero, the peer receives it, the peer acknowledges Initial, and the original loop receives that ACK.
- [ ] Assert the original loop's Initial sent tracker marks packet zero as acknowledged.
- [ ] Implement inbound ACK application through `sent_packet_tracker::on_ack_received`.
- [ ] Verify tests pass.

### Task 4: Handshake parity and errors

- [ ] Add a failing test that Handshake packets use an independent packet-number space starting at zero.
- [ ] Add a failing test that invalid datagrams produce `close_action` instead of received-frame actions.
- [ ] Implement Handshake queues/counters/trackers and close-on-error behavior.
- [ ] Verify tests pass.

## Verification

Run:

```powershell
$env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg
ctest --preset windows-msvc-vcpkg --timeout 10
```

If `clangd` is available, run LSP diagnostics on modified headers/tests. If not, record that LSP diagnostics were unavailable and rely on compiler/test output.

## Self-Review

- Spec coverage: The plan covers connection IDs, Initial/Handshake packet counters, ACK trackers, packet pipeline integration, datagram actions, and explicit non-goals.
- Placeholder scan: No TODO/TBD placeholders are present; deferred items are listed as out of scope.
- Type consistency: API names use existing FlowQ types (`frame`, `packet_number`, `packet_number_space`, `outbound_datagram`, `close_action`) and avoid duplicating packet-pipeline concepts.
