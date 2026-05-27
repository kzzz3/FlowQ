# M3b Recovery Core Design

## Goal

Extend M3a ACK/loss primitives into a larger, deterministic QUIC recovery core. M3b adds RTT estimation,
time-threshold loss detection, PTO deadline calculation, and pure loss-timer scheduling without implementing TLS,
packet protection, congestion control, stream retransmission, or ASIO timer ownership.

## RFC anchors

- RFC 9002 §4.1: loss detection is per packet number space; RTT is shared across spaces.
- RFC 9002 §5.1 and §5.3: RTT sampling, ACK-delay treatment, and estimator update formulas.
- RFC 9002 §6.1.1 and §6.1.2: packet-threshold and time-threshold loss detection.
- RFC 9002 §6.2.1 and Appendix A.8: PTO calculation and loss-detection timer selection.

## Non-goals

- No TLS handshake, packet protection, encryption level, key discard, or packet number reconstruction.
- No congestion control, cwnd, pacing, slow start, recovery windows, ECN, or persistent congestion.
- No stream retransmission policy, stream state, final-size validation, or flow-control accounting.
- No real timers, sockets, sleeps, ASIO integration, or packet sending on PTO.
- No ACK-frequency extension, adaptive reordering threshold, migration, or path validation.

## Files

- Extend `include/flowq/quic/ack_loss.hpp` with recovery timing value types and pure algorithms.
- Add `tests/unit/quic_recovery_core_tests.cpp` for M3b tests, keeping M3a tracker tests separate.
- Modify `tests/CMakeLists.txt` to compile the new recovery test file.
- Update `docs/development.md` with M3b scope and deferred behavior.

## Public API

```cpp
namespace flowq::quic {

struct rtt_sample {
    std::chrono::steady_clock::duration latest_rtt{};
    std::chrono::steady_clock::duration ack_delay{};
    std::chrono::steady_clock::duration peer_max_ack_delay{};
    bool handshake_confirmed{};
};

class rtt_estimator {
public:
    void update(const rtt_sample& sample);
    bool has_sample() const noexcept;
    std::chrono::steady_clock::duration latest_rtt() const noexcept;
    std::chrono::steady_clock::duration min_rtt() const noexcept;
    std::chrono::steady_clock::duration smoothed_rtt() const noexcept;
    std::chrono::steady_clock::duration rtt_variance() const noexcept;
};

struct recovery_packet {
    packet_number_space space{};
    std::uint64_t packet_number{};
    std::chrono::steady_clock::time_point sent_at{};
    bool ack_eliciting{};
    sent_packet_state state{sent_packet_state::outstanding};
};

struct time_loss_result {
    std::vector<std::uint64_t> newly_lost;
    std::optional<std::chrono::steady_clock::time_point> earliest_loss_time;
};

struct pto_config {
    std::chrono::steady_clock::duration max_ack_delay{};
    std::chrono::steady_clock::duration initial_rtt{333ms};
    std::uint32_t pto_count{};
    bool handshake_confirmed{};
};

enum class loss_timer_mode { none, loss_time, pto };

struct loss_timer_deadline {
    loss_timer_mode mode{loss_timer_mode::none};
    std::optional<std::chrono::steady_clock::time_point> deadline;
};

} // namespace flowq::quic
```

## RTT estimator

The first sample initializes:

- `latest_rtt = sample.latest_rtt`
- `min_rtt = sample.latest_rtt`
- `smoothed_rtt = sample.latest_rtt`
- `rtt_variance = sample.latest_rtt / 2`

For later samples:

- `min_rtt` uses raw RTT and never increases.
- ACK delay is clamped to `peer_max_ack_delay` only after handshake confirmation.
- ACK delay is subtracted only if `latest_rtt - min_rtt > ack_delay`.
- `rtt_variance = 3/4 * rtt_variance + 1/4 * abs(smoothed_rtt - adjusted_rtt)`.
- `smoothed_rtt = 7/8 * smoothed_rtt + 1/8 * adjusted_rtt`.

M3b accepts explicit RTT samples rather than deriving them from encrypted packet processing.

## Time-threshold loss

M3b adds a pure function that evaluates recovery packets with explicit timestamps:

```cpp
time_loss_result detect_time_threshold_losses(
    std::vector<recovery_packet>& packets,
    const rtt_estimator& estimator,
    packet_number_space space,
    std::uint64_t largest_acknowledged,
    std::chrono::steady_clock::time_point now);
```

```text
loss_delay = max(9/8 * max(latest_rtt, smoothed_rtt), 1ms)
loss_time = sent_at + loss_delay
```

An outstanding ack-eliciting packet sent before the largest acknowledged packet is newly lost when
`loss_time <= now`. Non-ack-eliciting, acknowledged, and already lost packets are ignored. Packets that are not yet
lost contribute the earliest future `loss_time` for timer scheduling.

## PTO calculation

M3b computes PTO deadlines without sending probes:

```text
base_pto = smoothed_rtt + max(4 * rtt_variance, 1ms) + max_ack_delay
deadline = last_ack_eliciting_sent_at + base_pto * 2^pto_count
```

If no RTT sample exists, `initial_rtt` is used as both smoothed RTT and twice the RTT variance basis, giving
`initial_rtt + max(4 * (initial_rtt / 2), 1ms)` before backoff and ACK delay. `max_ack_delay` is included only for
Application Data when the handshake is confirmed; Initial and Handshake PTO use zero ACK delay.

PTO expiry is not packet loss. The scheduler only reports the deadline and mode. Repeated scheduler polling must not
move a PTO deadline forward; the deadline is anchored to the last outstanding ack-eliciting packet's send time.

## Loss timer scheduler

The scheduler is a pure selector:

1. If any packet has an earlier time-threshold loss deadline, return `loss_timer_mode::loss_time` with that deadline.
2. Else if any outstanding ack-eliciting packet exists and PTO is allowed for the packet number space, return
   `loss_timer_mode::pto` with the PTO deadline anchored to the last ack-eliciting send time in that space.
3. Else return `loss_timer_mode::none`.

The existing `core::request_timer(timer_id::loss_detection, delay)` remains the future integration point; M3b does
not call it directly.

## Tests

Add deterministic tests for:

1. First RTT sample initializes estimator values.
2. Later RTT samples update smoothed RTT, RTT variance, and min RTT with ACK-delay clamping.
3. ACK delay is ignored before handshake confirmation.
4. Time-threshold loss marks only outstanding ack-eliciting packets and returns the next loss deadline.
5. Time-threshold loss is isolated by packet number space.
6. PTO uses RFC 9002 formula with and without RTT samples.
7. PTO backoff doubles deadlines by `2^pto_count`.
8. Scheduler prefers loss-time deadlines over PTO, returns no timer when no ack-eliciting packets are outstanding,
   and does not arm Application Data PTO before handshake confirmation.
9. Scheduler anchors PTO to packet send time rather than scheduler call time.

All tests must use fixed `std::chrono::steady_clock::time_point` values and must not sleep.

## Acceptance criteria

- `cmake --build --preset windows-msvc-vcpkg` succeeds.
- `ctest --preset windows-msvc-vcpkg --timeout 10` passes all existing and new tests.
- M3b documentation states that recovery timing is pure and does not include TLS, packet protection, congestion
  control, stream retransmission, or real timer ownership.
