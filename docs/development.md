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

## QUIC packet-pipeline scope

The M4a packet-pipeline stage connects the existing long-header codec and frame codec into a pure packet assembly and
parsing layer in `flowq::quic::packet_pipeline`. It is the first integrated path from structured frames to packet bytes
and back.

- `assemble_long_packet` concatenates encoded frame bytes, calls a `packet_protector`, prepends fixed 4-byte packet
  number metadata, builds Initial or Handshake long-header packets, and enforces a configured datagram size limit.
- Assembly rejects packet numbers that do not fit the fixed M4a encoding, packet-number spaces that do not match the
  selected long-header type, and protector levels that do not match the packet type, except for explicit plaintext
  `protection_level::none` test/dev paths.
- `parse_long_packet` decodes Initial or Handshake long-header packets, extracts the fixed packet number metadata,
  calls the same protection seam to unprotect the remaining payload, and decodes structured frames.
- `packet_protector` is a narrow seam for future TLS/AEAD/header protection integration. `plaintext_packet_protector`
  is explicit and reports `protection_level::none`; it must not be mistaken for real packet protection.
- Packet number encoding is fixed-width and structural in M4a. Future packet protection work must replace this with
  QUIC packet-number-length selection and reconstruction.

M4a intentionally defers real TLS, AEAD, header protection, short headers, key lifecycle, connection state, stream
state, flow control, congestion control, and public API design. Test-only protectors may transform bytes
deterministically, but production paths must not label plaintext as protected.

## QUIC minimal connection-loop scope

The M4b connection-loop stage adds a small, pure single-connection coordinator in `flowq::quic::connection`. It
connects M4a packet assembly/parsing with M3 ACK tracking without taking ownership of sockets, ASIO timers, TLS, or
application stream semantics.

- `connection_loop` owns local/remote connection IDs, a peer endpoint, independent Initial and Handshake packet-number
  counters, per-space sent-packet trackers, and per-space received-packet trackers.
- Callers queue Initial or Handshake frame vectors, then call `flush()` to produce deterministic `outbound_datagram`
  actions through `assemble_long_packet`. Packet numbers start at zero in each space and advance independently.
- Inbound datagrams are parsed through `parse_long_packet`, recorded in the matching received tracker, surfaced as
  `received_packet_event` actions, and scanned for ACK frames that update the matching sent tracker.
- `acknowledge(space)` emits an ACK-only Initial or Handshake packet from the recorded received-packet ranges when that
  space has received packets.
- Parse or assembly failures become `close_action` values instead of exceptions, preserving the protocol-core action
  seam.

M4b remains structural and deliberately incomplete. It does not implement TLS handshake progression, real AEAD/header
protection, short headers, Application Data, packet-number reconstruction, Retry validation, stream state, stream
reassembly, retransmission queues, flow control, congestion control, pacing, key discard, migration, listener demux, or
ASIO event-loop integration.

## QUIC STREAM reassembly-core scope

The M5a STREAM stage adds a pure receive-side stream core in `include/flowq/quic/stream.hpp` under `flowq::quic`. It
turns structural STREAM frames from the M2c codec into ordered byte deliveries that future connection/application APIs
can consume.

- `classify_stream_id` decodes the QUIC stream ID initiator and direction bits: client/server initiated and
  bidirectional/unidirectional.
- `stream_receive_state` tracks one stream's contiguous receive offset, buffered out-of-order ranges, final-size state,
  and closed state.
- STREAM data is delivered only as newly contiguous ordered bytes. Gaps are buffered until the missing prefix arrives;
  duplicate frames produce no new delivery.
- Identical overlapping bytes are accepted, while conflicting bytes at the same offset return `protocol_error`.
- FIN fixes final size to `offset + data.size()`. Later inconsistent FIN values or data beyond the known final size
  return `protocol_error`.
- `stream_receive_set` routes STREAM frames by stream ID to independent receive states.

M5a intentionally keeps STREAM behavior below connection/application policy. It does not implement flow-control windows,
`MAX_DATA`, `MAX_STREAM_DATA`, `RESET_STREAM`, `STOP_SENDING`, stream scheduling, prioritization, retransmission queues,
send buffering, short headers, Application Data packet handling, TLS, or public stream APIs.

## QUIC STREAM send-core scope

The M6 STREAM send stage extends `include/flowq/quic/stream.hpp` with a pure send-side stream core under `flowq::quic`.
It turns immutable application bytes into structural STREAM frames that existing packet assembly code can encode later.

- `stream_send_state` owns one stream's appended bytes, assigns stable offsets starting at zero, and emits
  `stream_frame` values with explicit lengths.
- Generated frames omit the offset field only for offset zero; non-zero offsets are encoded explicitly.
- `finish()` fixes the stream final size to the number of appended bytes. The final data frame or an empty FIN frame
  carries the FIN marker without changing that final size.
- `on_lost(range)` makes an emitted range retransmittable with the same bytes and offset; `on_acked(range)` suppresses
  later retransmission for acknowledged information.
- `stream_send_set` routes append/finish/pop operations to independent per-stream send states.
- Send frames are compatible with the M5a receive core, so tests can feed generated STREAM frames directly into
  reassembly and observe ordered bytes.

M6 remains below packet scheduling and connection policy. It does not implement flow-control credit, packet-to-stream
range mapping in `connection_loop`, congestion control, prioritization, RESET_STREAM, STOP_SENDING, short headers,
Application Data packet handling, TLS, or public stream APIs.
