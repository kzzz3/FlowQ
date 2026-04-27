# FlowQ Development

## Configure

FlowQ uses CMake and vcpkg manifest mode for third-party dependencies.

```powershell
cmake --preset windows-msvc-vcpkg
```

The default preset reads vcpkg from `VCPKG_ROOT`. If vcpkg is installed at `D:\vcpkg`, run:

```powershell
$env:VCPKG_ROOT = "D:/vcpkg"
```

If vcpkg is installed elsewhere, set `VCPKG_ROOT` to that location or configure manually with
`-DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake`.

## Build

```powershell
cmake --build --preset windows-msvc-vcpkg
```

## Test

```powershell
ctest --preset windows-msvc-vcpkg
```

## Dependency policy

- `asio` provides standalone Asio as the UDP/timer/event-loop foundation.
- `stdexec` is isolated behind FlowQ execution headers while the public API stabilizes.
- `catch2` is used for unit tests.

Keep protocol-core tests independent of sockets whenever possible. ASIO integration tests should verify
I/O and cancellation behavior separately from QUIC state-machine transitions.

## Current ASIO sender boundaries

The M1 ASIO senders intentionally use a minimal sender shape: `connect(receiver)` returns an operation
with `start()` and, where supported, `cancel()`. This is enough to test FlowQ completion semantics before
the project commits to full stdexec customization points.

Timer operations own their `asio::steady_timer`, so the timer stays alive until the handler completes.
Cancellation maps ASIO `operation_aborted` to `set_stopped()`.

UDP operations do not own the socket. Callers must keep the referenced `asio::ip::udp::socket` alive until
the operation completes or cancellation has been observed. Calling `cancel()` on a UDP operation delegates to
`socket.cancel()`, which cancels all outstanding operations on that socket, not only the operation object that
issued the call. Future protocol code should avoid sharing one socket across independently cancellable operations
without a higher-level cancellation coordinator.

## Protocol-core boundary

The M1.5 protocol core is intentionally socket-free and TLS-free. `flowq::quic::core` consumes owned,
synchronous input values such as inbound datagrams, timer firings, and application close requests. It emits a
deterministic FIFO list of pure actions that integration code can interpret later.

Current `received_datagram` and `timer_expired` actions are scaffolding for boundary tests, not QUIC packet
processing. They exist to prove data ownership, ordering, and observability before adding packet codecs,
TLS, ACK/loss recovery, streams, or congestion control. Keep future QUIC behavior behind this value-type seam
so protocol tests can run without ASIO sockets or executors.

## QUIC codec scope

The M2a codec stage implements byte-level QUIC primitives only. `flowq::quic::varint` supports RFC 9000
variable-length integer sizes of 1, 2, 4, and 8 bytes, encodes values minimally, accepts valid non-minimal
encodings on decode, and rejects truncation or values outside `0..2^62-1`.

`flowq::quic::frame` currently supports a deliberately tiny frame subset:

- `PADDING` (`0x00`) as a collapsed run count.
- `PING` (`0x01`) as a no-payload frame.
- Transport `CONNECTION_CLOSE` (`0x1c`) with error code, frame type, and reason phrase.

ACK, STREAM, CRYPTO, packet headers, packet numbers, TLS, packet protection, and loss recovery remain out of
scope for M2a. Unknown frame types and truncated frame payloads return structured `flowq::error` values instead
of throwing exceptions.

## QUIC packet-header codec scope

The M2b packet-header codec implements structural long-header envelopes only. `flowq::quic::packet_header`
supports Version Negotiation, Initial, Handshake, and Retry packets as owned value types. Initial and Handshake
keep packet-number bytes and encrypted payload together as an opaque protected payload; M2b does not reconstruct
packet numbers or inspect encrypted frames.

Short headers are intentionally rejected as unsupported because their Destination Connection ID length comes from
connection state and their packet-number/key-phase bits are header-protected. Header protection, TLS, packet
protection, Retry integrity validation, ACK/loss behavior, and payload frame processing remain future stages.

## QUIC structural frame expansion scope

The M2c frame expansion keeps `flowq::quic::frame` as a structural codec. It adds ACK `0x02`, CRYPTO `0x06`,
and STREAM `0x08..0x0f` frame variants, but does not attach protocol behavior to them.

- ACK frames preserve largest acknowledged, ACK delay, first range, and Gap/Range pairs. ACK ECN `0x03`, RTT
  calculation, packet loss recovery, and acknowledgement scheduling are out of scope.
- CRYPTO frames preserve offset and opaque byte payload. TLS parsing, handshake progression, encryption levels,
  and packet protection are out of scope.
- STREAM frames preserve stream ID, offset presence, explicit length presence, FIN, and byte payload. Stream
  reassembly, stream state, final-size validation, backpressure, and flow control are out of scope.

STREAM frames without the LEN bit consume the remaining frame-buffer bytes by design. Future packet-protection
or transport-state stages must define their own boundaries before interpreting these decoded values.

## QUIC ACK/loss core scope

The M3a ACK/loss stage adds pure, deterministic protocol primitives in `flowq::quic::ack_loss`. It remains below
packet protection and connection integration.

- `received_packet_tracker` records unique received packet numbers and builds structural ACK `0x02` ranges compatible
  with the M2c frame codec. Duplicate observations are ignored, and out-of-order packet numbers are normalized.
- `sent_packet_tracker` records sent packet numbers, applies peer ACK ranges, reports newly acknowledged packets, and
  marks outstanding ack-eliciting packets lost with an ACK-driven packet-threshold rule.
- Packet number spaces are modeled structurally as Initial, Handshake, and Application, but no encryption keys or TLS
  state are attached to them.

M3a intentionally defers RTT estimation, ACK-delay interpretation, time-threshold loss, PTO, persistent congestion,
congestion control, pacing, stream retransmission queues, flow control, TLS, packet protection, and ACK ECN handling.

## QUIC recovery-core scope

The M3b recovery stage extends `flowq::quic::ack_loss` with pure timing decisions from RFC 9002. It remains a
deterministic value-type layer and still does not own sockets, ASIO timers, TLS state, or packet protection.

- `rtt_estimator` tracks latest RTT, min RTT, smoothed RTT, and RTT variance from explicit samples. ACK delay is
  clamped to the peer max ACK delay only after handshake confirmation, and is never subtracted below min RTT.
- `detect_time_threshold_losses` evaluates explicit `recovery_packet` timestamps inside one packet number space,
  marks old outstanding ack-eliciting packets lost, and returns the earliest future loss-time deadline.
- `pto_deadline` computes probe timeout deadlines from smoothed RTT, RTT variance, granularity, peer max ACK delay,
  initial RTT, and PTO backoff. `next_loss_timer` anchors PTO to the last outstanding ack-eliciting packet's send
  time so repeated scheduler polling does not move the deadline forward. PTO expiry is reported only as a deadline;
  M3b does not send probe packets.
- `next_loss_timer` is a pure selector that prefers loss-time deadlines over PTO and does not arm Application Data PTO
  before handshake confirmation.

M3b intentionally defers congestion control, persistent congestion, pacing, stream retransmission policy, actual timer
arming, probe packet construction, TLS, packet protection, key discard, migration, ECN, and ACK-frequency behavior.
