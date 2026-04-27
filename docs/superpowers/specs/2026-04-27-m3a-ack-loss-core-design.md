# M3a ACK/Loss Core Design

## Goal

Add deterministic ACK/loss core primitives that build on the M2c structural ACK frame codec without entering TLS,
packet protection, stream state, flow control, RTT estimation, PTO, or congestion control.

This milestone is an ACK-driven loss tracker. It records sent packet metadata, applies peer ACK frames, and marks
packets lost using a packet-threshold rule. It also tracks received packet numbers and produces ACK frame ranges for
future packet writers.

## RFC anchors

- RFC 9000 §12.3: packet numbers are monotonic within a packet number space and are not reused.
- RFC 9000 §12.5 and RFC 9002 §4.1: Initial, Handshake, and Application Data use separate packet number spaces;
  0-RTT shares the Application Data space.
- RFC 9000 §19.3 and §19.3.1: ACK frames encode the largest acknowledged packet plus descending ACK ranges.
- RFC 9002 §6.1.1: packet-threshold loss marks unacknowledged packets sufficiently below the largest newly
  acknowledged packet.

## Non-goals

- No TLS handshake, CRYPTO stream processing, key derivation, AEAD, header protection, or packet-number decoding.
- No RTT estimator, ACK-delay interpretation, time-threshold loss, PTO, persistent congestion, pacing, or cwnd.
- No stream retransmission queue, stream lifecycle, final-size validation, or flow-control accounting.
- No transport integration with ASIO sockets or timers. The existing `timer_id::loss_detection` remains a future hook.
- No ACK ECN processing. M2c still rejects ACK ECN frame type `0x03`.

## Files

- Create `include/flowq/quic/ack_loss.hpp` for pure ACK/loss data structures and algorithms.
- Create `tests/unit/quic_ack_loss_tests.cpp` for deterministic unit tests.
- Modify `tests/CMakeLists.txt` to compile the new test file.
- Update `docs/development.md` with the M3a scope and explicit deferred behavior.

## Public types

```cpp
namespace flowq::quic {

enum class packet_number_space {
    initial,
    handshake,
    application
};

enum class sent_packet_state {
    outstanding,
    acknowledged,
    lost
};

struct sent_packet {
    packet_number_space space{};
    std::uint64_t packet_number{};
    bool ack_eliciting{};
    sent_packet_state state{sent_packet_state::outstanding};
};

struct loss_detection_result {
    std::vector<std::uint64_t> newly_acknowledged;
    std::vector<std::uint64_t> newly_lost;
};

class received_packet_tracker;
class sent_packet_tracker;

} // namespace flowq::quic
```

`packet_number_space` is structural only. It does not imply encryption keys or packet-protection state.

## Received packet tracker

`received_packet_tracker` records packet numbers observed in one packet number space. Duplicate observations are
ignored. The tracker stores unique packet numbers and can produce a structural `ack_frame` compatible with M2c.

Required API:

```cpp
class received_packet_tracker {
public:
    bool observe(std::uint64_t packet_number);
    bool empty() const noexcept;
    ack_frame to_ack_frame(std::uint64_t ack_delay = 0) const;
};
```

### ACK range construction

For a sorted received set `{1, 2, 5, 6}`, `to_ack_frame()` returns:

- `largest_acknowledged = 6`
- `first_ack_range = 1`, representing packets `6..5`
- one additional `ack_range{gap = 1, length = 1}`, representing packets `2..1`

The additional ACK range follows RFC 9000 encoding: after the previous smallest acknowledged packet `5`, `gap = 1`
skips two packet numbers, `4` and `3`; `length = 1` then covers two packets, `2` and `1`.

For contiguous `{1, 2, 3, 4}`, the frame has `largest_acknowledged = 4`, `first_ack_range = 3`, and no additional
ranges. Out-of-order input is normalized before range generation.

## Sent packet tracker

`sent_packet_tracker` records locally sent packet numbers for one packet number space. It applies a peer `ack_frame`
and marks packets acknowledged or lost. The tracker does not store frame payloads and cannot retransmit data.

Required API:

```cpp
class sent_packet_tracker {
public:
    explicit sent_packet_tracker(packet_number_space space);

    void on_packet_sent(std::uint64_t packet_number, bool ack_eliciting);
    loss_detection_result on_ack_received(const ack_frame& frame, std::uint64_t packet_threshold = 3);

    const std::vector<sent_packet>& packets() const noexcept;
};
```

### ACK application

The tracker expands `ack_frame` ranges into acknowledged packet numbers. It only mutates packets in its own space.
Unknown acknowledged packet numbers are ignored because packet protection and full connection validation are outside
this milestone. Repeated ACKs are idempotent: a packet already acknowledged or lost is not reported again.

### Packet-threshold loss

On ACK receipt, the tracker computes the largest acknowledged packet from the ACK frame. An outstanding,
ack-eliciting packet is newly lost when:

```text
packet_number + packet_threshold <= largest_acknowledged
```

Only ack-eliciting packets are marked lost. Non-ack-eliciting packets can be acknowledged, but M3a does not report
them as lost. Time-threshold loss and PTO are explicitly deferred.

Example with sent packets `1, 2, 3, 4, 5`, ACK for `5`, and threshold `3`:

- newly acknowledged: `{5}`
- newly lost: `{1, 2}`
- packets `3` and `4` remain outstanding.

## Error handling

The tracker APIs are deterministic and do not throw. Invalid ACK-frame shapes that cannot be expanded safely, such as
a first ACK range larger than `largest_acknowledged`, are treated as acknowledging nothing and losing nothing in M3a.
Codec-level validation remains in `frame.hpp`; richer protocol-error reporting can be added when the QUIC connection
state machine exists.

## Tests

Add unit tests for:

1. Contiguous receive range generation.
2. Out-of-order receive normalization.
3. Gapped receive ACK range generation using QUIC gap/range lengths.
4. Duplicate receive idempotence.
5. ACK application marking newly acknowledged packets exactly once.
6. Packet-threshold loss marking only outstanding ack-eliciting packets.
7. Non-ack-eliciting packets are not reported lost by M3a.

All tests must run without sockets, timers, TLS credentials, sleeps, or wall-clock time.

## Acceptance criteria

- `cmake --build --preset windows-msvc-vcpkg` succeeds.
- `ctest --preset windows-msvc-vcpkg --timeout 10` passes all existing and new tests.
- M3a documentation clearly states that RTT, PTO, congestion control, TLS, stream state, and flow control are deferred.
