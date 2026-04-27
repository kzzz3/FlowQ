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

## QUIC STREAM flow-control/lifecycle-core scope

The M7 STREAM stage adds minimal pure stream-level flow-control accounting and FIN lifecycle inspection to
`include/flowq/quic/stream.hpp`. It keeps the M5a/M6 stream cores deterministic and value-oriented while making send and
receive behavior respect stream byte-credit limits.

- `stream_receive_state` can be constructed with a local stream receive limit. Incoming STREAM data ending at that limit
  is accepted; data whose end offset exceeds it returns `flow_control_error` without mutating buffered stream bytes.
- `stream_receive_state::update_max_data()` raises receive credit monotonically, so stale smaller limits do not shrink the
  allowed byte range.
- `stream_send_state` can be constructed with peer stream credit. New STREAM data is clipped by both caller payload limit
  and peer credit; when credit is exhausted, `pop_frame()` returns no frame instead of failing.
- `stream_send_state::update_max_data()` raises peer stream credit monotonically and lets pending bytes resume at stable
  offsets.
- Already-sent lost ranges remain retransmittable even when new unsent bytes are currently blocked by credit.
- Send lifecycle inspectors expose `finished()`, `fin_sent()`, `fin_acked()`, `closed()`, and `blocked()` without adding a
  public async stream API.
- `stream_receive_set` and `stream_send_set` route initial stream credit and credit updates to independent per-stream
  states.

M7 remains a stream-core milestone, not wire-level QUIC flow-control integration. It does not implement `MAX_DATA`,
`MAX_STREAM_DATA`, `DATA_BLOCKED`, `STREAM_DATA_BLOCKED`, connection-level flow control, packet-to-stream ACK/loss mapping
inside `connection_loop`, congestion control, prioritization, RESET_STREAM, STOP_SENDING, short headers, Application Data
packet handling, TLS, or public stream APIs.

## QUIC flow-control frame-codec scope

The M8 frame-codec stage extends `include/flowq/quic/frame.hpp` with structural wire support for QUIC byte-credit
flow-control signaling frames. It turns the four RFC 9000 byte-credit frame types into inert value objects that can be
encoded, decoded, and mixed with existing structural frame values.

- `max_data_frame` encodes and decodes `MAX_DATA` (`0x10`) with a `maximum_data` varint.
- `max_stream_data_frame` encodes and decodes `MAX_STREAM_DATA` (`0x11`) with `stream_id` and
  `maximum_stream_data` varints.
- `data_blocked_frame` encodes and decodes `DATA_BLOCKED` (`0x14`) with a `maximum_data` varint.
- `stream_data_blocked_frame` encodes and decodes `STREAM_DATA_BLOCKED` (`0x15`) with `stream_id` and
  `maximum_stream_data` varints.
- Malformed or truncated flow-control frame fields return structural codec errors, matching the existing frame codec
  style.
- Stream-count flow-control frames (`MAX_STREAMS` and `STREAMS_BLOCKED`) remain unsupported in M8 because stream-opening
  limits require a separate connection/stream policy stage.

M8 remains a codec-only milestone. It does not apply decoded `MAX_STREAM_DATA` to M7 stream send state, emit blocked
frames from M7 blocked state, implement connection-level flow-control accounting, integrate with `connection_loop`, add
packet-type legality checks, support short headers or Application Data packets, implement TLS/AEAD/header protection,
congestion control, RESET_STREAM, STOP_SENDING, stream scheduling, or public APIs.

## QUIC STREAM flow-control signal-seam scope

The M9 STREAM flow-control signal stage connects the M7 stream send-credit core to the M8 structural flow-control frame
values without adding packet scheduling or connection policy. It is a pure stream-scope adapter seam in
`include/flowq/quic/stream.hpp`.

- `stream_send_state::update_max_data(const max_stream_data_frame&)` applies peer stream credit when the frame stream ID
  matches the send state, while mismatched frames remain inert.
- `stream_send_set::update_max_data(const max_stream_data_frame&)` routes peer stream-credit updates by `stream_id`.
- Stale or lower `MAX_STREAM_DATA` values remain harmless because stream send credit is still monotonic.
- `stream_send_state::blocked_frame()` returns `stream_data_blocked_frame{stream_id, maximum_stream_data}` only when
  unsent bytes are blocked by the current stream credit limit.
- `stream_send_set::blocked_frame(stream_id)` reports blocked state for an existing selected stream and returns no frame
  for absent streams.

M9 remains a signal seam, not connection integration. It does not apply `MAX_DATA` or `DATA_BLOCKED`, implement
connection-level flow-control accounting, receive peer `STREAM_DATA_BLOCKED` as policy, schedule or retransmit control
frames, integrate with `connection_loop`, add packet-type legality checks, support short headers or Application Data
packets, implement TLS/AEAD/header protection, congestion control, RESET_STREAM, STOP_SENDING, stream scheduling, or
public APIs.

## QUIC STREAM outbound frame scheduler seam scope

The M10 STREAM scheduler stage adds a pure outbound batching helper to `include/flowq/quic/stream.hpp`. It turns selected
stream send states into deterministic vectors of existing structural `flowq::quic::frame` values that a future packet or
Application Data scheduler can consume.

- `stream_send_set::pop_frames()` iterates caller-provided stream IDs in order, keeping scheduling deterministic and free
  of hidden priority policy.
- STREAM data is produced through the existing `stream_send_state::pop_frame()` path, so offsets, FIN behavior,
  retransmission priority, and stream-level credit enforcement remain centralized in the send core.
- When no STREAM data can be emitted for a selected blocked stream, the scheduler can include the existing M9
  `STREAM_DATA_BLOCKED` signal value.
- `max_frames` bounds the number of complete frame values returned by one scheduling call. Byte-level packet payload
  budgeting remains future packet-scheduler work because encoded frame overhead depends on packet construction choices.
- Empty selections, zero frame budget, absent streams, and unblocked drained streams return successful empty batches rather
  than protocol errors.

M10 remains below packet scheduling and connection integration. It does not implement encoded frame byte-budget fitting,
`MAX_DATA`, `DATA_BLOCKED`, connection-level flow control, `connection_loop` integration, packet-type legality,
Application Data packets, short headers, TLS, AEAD/header protection, congestion control, RESET_STREAM, STOP_SENDING,
prioritization policy beyond caller order, blocked-frame deduplication, or public APIs.

## QUIC connection/STREAM integration seam scope

The M11 connection/STREAM integration stage connects the structural connection loop to the pure STREAM cores without
introducing real Application Data packet protection or public stream APIs.

- `connection_loop` owns `stream_receive_set` and `stream_send_set` instances in addition to its existing packet trackers.
- Inbound decoded `STREAM` frames are routed into `stream_receive_set` only after duplicate packet suppression, preserving
  existing packet-event behavior while exposing ordered `stream_delivery` values on `received_packet_event`.
- Inbound decoded `MAX_STREAM_DATA` frames update connection-owned stream send credit through the existing M9 stream signal
  seam.
- `append_stream_data()` and `schedule_stream_frames()` are synchronous connection helpers that delegate to the M6/M10
  stream send core. They return structural frame values only and do not queue or flush packets by themselves.
- `connection_loop_config::initial_stream_send_max_data` lets tests model peer stream credit at connection construction
  time without adding transport-parameter negotiation.

M11 remains a structural integration seam. It does not implement Application packet space, short headers, real TLS,
AEAD/header protection, connection-level `MAX_DATA` / `DATA_BLOCKED`, packet byte-budget fitting, congestion control,
RESET_STREAM, STOP_SENDING, public async stream APIs, sockets, or production interoperability.

## QUIC connection-level flow-control seam scope

The M12 connection-level flow-control stage adds aggregate outbound STREAM byte-credit accounting to
`connection_loop`. It composes with the M7/M9 stream-level credit rules instead of replacing them.

- `connection_loop_config::initial_connection_send_max_data` lets tests model peer connection credit before transport
  parameter negotiation exists.
- `connection_loop::schedule_stream_frames()` caps emitted STREAM bytes by the remaining connection-level credit before
  delegating to the stream scheduler, so stream offsets remain owned by `stream_send_state`.
- `MAX_DATA` is applied monotonically through both `connection_loop::update_max_data()` and inbound decoded
  `max_data_frame` values on the packet path.
- Connection-level accounting consumes only STREAM payload bytes. Control frames such as `STREAM_DATA_BLOCKED` do not
  consume aggregate data credit.
- When aggregate credit is exhausted and selected streams still have unsent data, scheduling can return one structural
  `DATA_BLOCKED` frame with the current absolute maximum.
- If stream credit is exhausted before connection credit, existing `STREAM_DATA_BLOCKED` behavior is preserved.

M12 remains a structural flow-control seam. It does not implement receive-window growth policy, `MAX_STREAMS` /
`STREAMS_BLOCKED`, encoded packet byte-budget fitting, Application Data packets, short headers, TLS, AEAD/header
protection, congestion control, pacing, blocked-frame deduplication, RESET_STREAM, STOP_SENDING, public async stream APIs,
sockets, or production interoperability.

## QUIC packet byte-budget scheduler scope

The M13 packet byte-budget scheduler stage adds a pure encoded-frame payload budget helper and applies it to queued
Initial/Handshake frame flushing in `connection_loop`.

- `select_frames_for_payload_budget()` measures each candidate with the existing structural frame encoder, preserves input
  order, selects only complete frames, and stops before the first frame that would exceed the caller's payload budget.
- The helper reports the selected frames, their total encoded size, the next unselected candidate index, and any frame
  encode error using the existing result-style `ok()` convention.
- Exact-fit frames are selected. A first non-fitting frame produces a successful empty helper selection so callers can
  decide their own policy.
- `connection_loop_config::max_packet_payload_size` lets tests cap queued Initial/Handshake frame payload bytes before
  packet assembly.
- `flush_space()` assembles only the selected prefix and leaves non-selected queued frames for a later flush, preserving
  packet-number ordering and avoiding partial frame emission.

M13 budgets encoded frame payload bytes only. It does not account for long-header bytes, fixed packet-number metadata,
packet protection overhead, UDP datagram PMTU, Initial minimum-size padding, coalescing, congestion control, pacing,
Application Data packets, short headers, TLS, AEAD/header protection, public async stream APIs, sockets, or production
interoperability.

## QUIC recovery timer integration scope

The M14 recovery timer integration stage wires the existing M3b recovery helpers into `connection_loop` as deterministic
value queries and timer-expiry results. It still does not own ASIO timers or construct retransmission packets.

- `connection_loop::flush(sent_at)` is a deterministic overload for tests and integration seams; the existing `flush()`
  remains available and stamps sent packets with `steady_clock::now()`.
- `connection_loop` records per-packet recovery metadata for Initial and Handshake packets alongside the existing sent
  packet trackers.
- `next_recovery_timer(now)` returns the earliest Initial/Handshake recovery deadline as a value containing packet number
  space, timer mode, and deadline. Polling the query does not mutate packet timestamps or move PTO anchors.
- Inbound ACK processing synchronizes recovery metadata so acknowledged packets stop keeping timers alive.
- `on_recovery_timer(space, now)` runs time-threshold loss detection for the selected space and returns newly lost packet
  numbers without building PTO probes or retransmission packets.
- ACK-only packets remain non-ack-eliciting and do not arm recovery timers.

M14 remains a deterministic recovery-integration seam. It does not own real timers, schedule ASIO operations, build probe
packets, perform stream retransmission, implement congestion control, pacing, persistent congestion, ECN, Application Data
packet space, short headers, TLS, AEAD/header protection, public async APIs, sockets, or production interoperability.

## QUIC STREAM ACK/loss mapping scope

The M15 STREAM ACK/loss mapping stage records which STREAM information was carried by sent Initial/Handshake packets and
routes later packet outcomes back into `stream_send_set`.

- `connection_loop` keeps a packet-to-stream ledger keyed by packet number space and packet number. Each ledger entry stores
  stream ID, offset, length, and FIN for STREAM frames that were actually selected into a sent packet.
- `sent_stream_ranges(space, packet_number)` exposes a value snapshot for deterministic tests.
- Inbound ACK handling routes newly acknowledged packet numbers to `stream_send_set::on_acked()`, suppressing later
  retransmission of acknowledged stream information.
- Packet-threshold loss from ACK processing and time-threshold loss from `on_recovery_timer()` route newly lost packet
  numbers to `stream_send_set::on_lost()`, allowing the existing stream scheduler to retransmit the same stream offset,
  bytes, and FIN information with a future packet number.
- Lost STREAM retransmissions are scheduled from already-sent stream ranges, so they do not consume fresh connection-level
  flow-control credit or advance the aggregate sent-data counter again.
- Stream send state validates ACK/loss ranges against bytes already emitted by the scheduler. Late ACKs for a lost FIN
  suppress the queued FIN retransmission, including after partial data retransmission, while ACKs for FIN ranges that were
  neither sent nor marked lost remain ignored. Loss signals for unsent empty FIN ranges are also ignored so the first FIN
  emission remains a normal send, not a retransmission.
- Non-STREAM frames do not create stream ledger entries, and M13 packet-budget splitting records only frames included in the
  selected sent prefix. Manually queued STREAM frames that do not belong to `stream_send_set` do not create send state when
  packet outcomes are mapped back.

M15 retransmits stream information through existing stream send state; it does not construct retransmission packets, refund
connection flow-control credit, add congestion or pacing policy, implement RESET_STREAM or STOP_SENDING, add Application
Data packet space, short headers, TLS, AEAD/header protection, public async APIs, sockets, or production interoperability.

## QUIC Application Data structural packet-space scope

The M16 Application Data structural packet-space stage adds an explicit non-production Application packet path for local
tests and future loopback development.

- `packet_pipeline` exposes `application_packet_build_request`, `assemble_application_packet()`, and
  `parse_application_packet()` for a structural Application envelope carried behind the existing `packet_protector` seam.
- Structural Application packets use packet number space `application`, keep packet numbers independent from Initial and
  Handshake, and can carry frames such as STREAM and ACK in deterministic tests.
- `connection_loop` owns an Application queue, packet counter, sent tracker, receive tracker, and largest-ACKed value so
  Application ACKs do not alias Initial or Handshake packet state.
- `queue_application()`, `flush()`, `on_datagram()`, `acknowledge(application)`, and `sent_packets(application)` support the
  structural Application path.
- `decode_packet_header()` still rejects real short headers. The structural marker used by M16 is a test envelope, not an
  RFC-valid 1-RTT packet header.

M16 is not secure or interoperable QUIC. It does not implement real 1-RTT short-header encoding, packet number
reconstruction, key phase, TLS 1.3, AEAD, header protection, congestion control, public async APIs, sockets, or production
interoperability.

## QUIC minimal close/reset structural codec scope

The M17 close/reset stage adds deterministic structural `RESET_STREAM` and `STOP_SENDING` behavior for tests and future
loopback work.

- `frame.hpp` now structurally encodes and decodes `reset_stream_frame` (`0x04`) and `stop_sending_frame` (`0x05`).
- `stream_receive_state::reset()` records the reset application error code and final size, exposes reset queries, and rejects
  later STREAM data after reset.
- `stream_send_state::stop_sending()` records the stop application error code, suppresses new STREAM emission, clears queued
  retransmission ranges, and rejects later append/finish attempts.
- `stream_receive_set` and `stream_send_set` route reset/stop frames by stream ID.
- `connection_loop` applies inbound reset/stop frames while preserving the existing `received_packet_event` raw-frame surface
  for deterministic tests.

M17 remains structural. It does not add an application callback policy, automatic RESET_STREAM generation in response to
STOP_SENDING, a complete stream lifecycle state machine, application CONNECTION_CLOSE (`0x1d`), public async stream APIs,
sockets, TLS, AEAD, header protection, or production interoperability.

## QUIC in-memory loopback session scope

The M18 loopback stage proves the current structural connection pieces can form a basic usable non-production session in
deterministic tests. The loopback harness lives in `tests/integration/quic_loopback_tests.cpp` and connects two
`connection_loop` instances by moving `outbound_datagram` actions directly into the peer `on_datagram()` path.

- Client-to-server and server-to-client STREAM data are exchanged over the M16 structural Application packet envelope and
  delivered through `received_packet_event::stream_deliveries`.
- Application ACKs are pumped back through the peer connection and update the sender's Application sent-packet tracker.
- Deterministic packet loss is modeled by dropping an outbound datagram, delivering a later Application packet/ACK, then
  expiring the existing recovery timer seam so M15 packet-to-stream loss mapping reschedules the lost stream bytes.
- Stream flow control is exercised end to end: initial stream credit emits a prefix, an inbound `MAX_STREAM_DATA` frame raises
  credit, and a later Application packet carries the suffix at the original stream offset.
- Application `RESET_STREAM` is observable on the peer receive stream state, preserving the M17 structural reset semantics in
  the loopback path.

M18 is still intentionally local and unsafe. It does not add real UDP sockets, TLS 1.3, AEAD, header protection, real QUIC
short headers, address validation, congestion control, HTTP/3, public async stream APIs, or production interoperability.

## QUIC crypto adapter seam scope

The M19 crypto adapter seam makes packet-protection capability explicit without implementing production cryptography. It
keeps FlowQ's deterministic tests usable while preventing plaintext/test protection from satisfying paths that explicitly
require production packet protection.

- `packet_security_level` distinguishes `test_only` protectors from `authenticated_encrypted` adapters.
- `plaintext_packet_protector` remains available for deterministic tests, reports `protection_level::none`, and reports
  `packet_security_level::test_only`.
- `packet_protection_policy::production_required` makes packet assembly/parsing reject test-only protectors with a structured
  protocol error. Existing tests and loopback paths use the default `test_allowed` policy intentionally.
- `connection_loop_config::protection_policy` forwards the same policy to Initial, Handshake, and structural Application
  assembly/parsing so future production-facing configurations cannot silently reuse plaintext protection.
- Fake adapter tests prove the seam can accept an external authenticated/encrypted implementation through the existing
  `packet_protector` interface.

Real QUIC security still requires an external TLS 1.3 and crypto backend. A future adapter must own TLS handshake/key
material, encryption-level keys, AEAD packet protection, header protection, packet-number reconstruction, key phase/key
updates, test vectors, and interoperability work. M19 does not implement TLS, AEAD, header protection, certificate
validation, real short headers, UDP APIs, or production interoperability.

## QUIC basic-complete library scope freeze

The M20 scope-freeze stage defines what remains before FlowQ can be called a basic complete non-production QUIC-like C++
library baseline. It is a documentation and roadmap milestone only; it does not add the public session façade, UDP adapter,
examples, packaging, or CI yet.

- M18 was the basic usable in-memory loopback proof.
- M19 made packet-protection capability explicit and kept plaintext protection test-only.
- M20 freezes the basic-complete library target: public session API, bounded non-production UDP/ASIO smoke path, recovery
  scheduler adapter, examples, CMake install/export packaging, CI, and release-scope documentation.
- M21-M26 implement that target in order: session façade, UDP adapter, recovery scheduler, examples, package export, and CI
  plus basic-complete docs.
- Production QUIC remains out of scope for the basic-complete baseline: real TLS 1.3, AEAD, header protection, RFC-valid
  short-header packet-number reconstruction, congestion control, HTTP/3, and interoperability are future tracks.

When this scope changes, update `README.md`, `PLAN.md`, `docs/development.md`, the M20 scope spec, and the post-M19
completion plan together so the repository keeps a single coherent roadmap.

## QUIC public session façade scope

M21 adds a small synchronous public façade over `flowq::quic::connection_loop`. It gives library consumers a stable value API
for common stream actions without requiring them to build raw frame queues directly.

- `include/flowq/quic/events.hpp` defines public session result values, including stream deliveries and outbound datagrams.
- `include/flowq/quic/session.hpp` defines `session_config` and `session`; `session_config` mirrors the connection-loop
  inputs needed by the current deterministic pipeline: role, version, connection IDs, peer endpoint, protector pointers,
  packet pipeline settings, flow-control limits, payload budget, and packet-protection policy.
- `session::append_stream_data` appends user data to a stream send state; `session::queue_stream_data` schedules stream IDs
  into the Application queue; `session::flush` is the explicit step that assembles queued frames into outbound datagrams.
- `session::on_datagram` translates received connection-loop packet events into public stream-delivery values, while
  `session::acknowledge` returns ACK datagrams through the same public result shape.
- Recovery timer values remain exposed as deterministic connection-loop values for later scheduling work. Application PTO is
  not armed before handshake confirmation, matching the existing ACK/loss rules.

M21 still does not add UDP sockets, ASIO receive loops, TLS 1.3, AEAD, header protection, congestion control, HTTP/3, or
production interoperability. M22 is responsible only for a bounded non-production UDP/ASIO smoke adapter over this façade.

## QUIC non-production UDP session adapter scope

M22 adds a bounded ASIO UDP adapter for local smoke testing over the public `flowq::quic::session` façade. It is socket
plumbing around the deterministic session model, not a production QUIC transport.

- `include/flowq/quic/udp_session.hpp` defines `udp_session_config` and `udp_session`.
- `udp_session` binds an externally owned `asio::ip::udp::socket&`; callers keep ownership of the socket, executor, and
  lifecycle.
- `udp_session::send_pending` flushes the wrapped session and asynchronously sends the resulting datagrams to the configured
  ASIO peer endpoint while keeping datagram bytes alive until completion.
- `udp_session::async_receive_once` receives one UDP datagram, converts the sender endpoint into a `flowq::endpoint`, and
  passes the payload into `session::on_datagram`.
- The integration smoke test uses two loopback UDP sockets and plaintext/test-only packet protection to deliver one structural
  Application stream payload.

M22 intentionally does not add DNS resolution, connection establishment semantics, TLS 1.3, AEAD, header protection,
congestion control, HTTP/3, or interoperability claims. M23 handles timer scheduling separately.

## QUIC recovery scheduler adapter scope

M23 adds an ASIO scheduling adapter for recovery timer values that are already computed by the deterministic QUIC session and
connection-loop code. The adapter does not implement new recovery, PTO, congestion-control, or packet-loss algorithms.

- `include/flowq/quic/recovery_scheduler.hpp` defines `schedule_recovery` and `recovery_scheduler_result`.
- `schedule_recovery(context, session, now)` reads `session.next_recovery_timer(now)` once when the operation starts.
- If the session has no timer, the operation completes immediately with an empty successful result.
- If a timer exists, the adapter arms an `asio::steady_timer` for the timer's existing absolute deadline and calls
  `session.on_recovery_timer(timer.space, timer.deadline)` when ASIO reports expiry.
- Cancellation maps to `set_stopped`, matching the existing timer sender behavior.

M23 keeps recovery semantics in `connection_loop` and `session`. It does not add UDP behavior, production timer policy,
congestion control, or QUIC interoperability guarantees.

## QUIC examples and public smoke tests scope

M24 adds buildable examples and public smoke tests for the existing non-production QUIC-like library surface.

- `examples/in_memory_loopback.cpp` demonstrates two `quic::session` objects exchanging one structural Application stream
  payload entirely in memory with plaintext/test-only packet protection.
- `examples/udp_stream_echo.cpp` demonstrates two local UDP sockets connected through `quic::udp_session` for a bounded
  loopback smoke path.
- `examples/protection_policy.cpp` demonstrates that `packet_protection_policy::test_allowed` accepts plaintext/test-only
  protection while `packet_protection_policy::production_required` rejects it.
- The default Windows preset builds the example targets and CTest runs them as smoke tests when tests are enabled.
- `tests/integration/example_build_tests.cpp` keeps example-facing public API snippets compiling under Catch2.

The examples are not production QUIC, not interoperability tests, and do not implement TLS 1.3, AEAD, header protection,
congestion control, HTTP/3, DNS resolution, or production UDP transport semantics. M25 packaging and M26 CI/release docs
remain separate milestones.

## CMake install and package-consumer scope

M25 makes FlowQ consumable as a CMake package while keeping CI/release declaration work for M26.

- `CMakeLists.txt` installs the public `include/` tree and exports the `flowq` interface target as `FlowQ::flowq`.
- `cmake/FlowQConfig.cmake.in` loads FlowQ's public Asio and stdexec dependencies before including `FlowQTargets.cmake`.
- `FlowQConfigVersion.cmake` is generated with the project version and marked architecture-independent because FlowQ is a
  header-only interface library.
- `tests/package-consumer/` is an external-style CMake project that uses `find_package(FlowQ CONFIG REQUIRED)`, links
  `FlowQ::flowq`, includes `<flowq/quic/session.hpp>`, constructs a `session_config`, and exits without opening sockets.

Local M25 verification commands:

```powershell
$env:VCPKG_ROOT = "D:/vcpkg"
cmake --build --preset windows-msvc-vcpkg
cmake --install build/windows-msvc-vcpkg --config Debug --prefix build/install-flowq
cmake -S tests/package-consumer -B build/package-consumer -G "Visual Studio 18 2026" -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DCMAKE_PREFIX_PATH="F:/Project/FlowQ/build/install-flowq"
cmake --build build/package-consumer --config Debug
```

Consumers should configure with a vcpkg toolchain or otherwise provide compatible Asio and stdexec CMake packages. M25 does
not add CI automation; M26 owns the reproducible workflow and basic-complete release documentation.

## CI and basic-complete baseline

M26 adds the reproducible CI gate and declares FlowQ's basic-complete non-production baseline.

- `.github/workflows/ci.yml` runs on a Windows MSVC/vcpkg runner and executes the same gate expected locally: configure,
  build, CTest, install, package-consumer configure/build, and package-consumer execution.
- `docs/basic-complete.md` is the release-scope baseline document. It lists what FlowQ supports today, what remains
  explicitly unsupported, the public header surface, examples, build/test gates, and the future production QUIC backlog.
- CI does not publish packages, deploy artifacts, require secrets, or claim production QUIC readiness.
- The CI package-consumer step verifies that the installed `FlowQ::flowq` target can be found from a separate CMake project
  when the same vcpkg toolchain is available.

The baseline remains non-production. It does not implement TLS 1.3, AEAD, header protection, congestion control, HTTP/3,
DNS resolution, interoperability, or production UDP transport semantics. Plaintext packet protection remains test-only, and
`packet_protection_policy::production_required` continues to reject test-only protection.

## Post-basic production-readiness roadmap

After M26, FlowQ moves toward production-readiness in separately reviewable milestones. This does not change the current
status: FlowQ remains non-production until TLS/crypto, short headers, congestion control, and interoperability have executable
evidence.

- M27 adds RFC 9000 packet-number helpers for length selection, truncation, and reconstruction.
- M28 should define a provider boundary for vetted crypto libraries; FlowQ must not implement AES, ChaCha20, Poly1305,
  HKDF, TLS 1.3, certificate validation, or random number generation by hand.
- M29 should use that provider boundary to pass selected RFC 9001 packet-protection vectors.
- M31+ should cover TLS handshake adapter state, RFC-shaped short headers, congestion control, and
  interoperability harnesses as separate milestones.
- The complete route from M28 through M39 is documented in
  `docs/superpowers/plans/2026-04-27-post-basic-production-readiness-roadmap.md`.

### M27 packet-number helper scope

M27 adds pure packet-number arithmetic helpers in `include/flowq/quic/packet_header.hpp`:

- `select_packet_number_length(packet_number, largest_acknowledged)` selects a 1-4 byte packet-number encoding window large
  enough for QUIC reconstruction around the largest acknowledged packet.
- `encode_packet_number(packet_number, length, output)` writes the selected least significant packet-number bytes in
  network byte order.
- `decode_packet_number(truncated_packet_number, length, largest_received)` reconstructs the full packet number around
  `largest_received + 1` using the RFC 9000 window algorithm.

These helpers do not decode real short headers, apply header protection, perform AEAD, or make FlowQ production-ready. They
are a deterministic prerequisite for future short-header and header-protection milestones.

### M28 crypto provider boundary scope

M28 adds a provider-capability boundary in `include/flowq/quic/crypto_provider.hpp` and tightens the packet-protection
policy gate in `include/flowq/quic/packet_pipeline.hpp`.

- `cipher_suite`, `crypto_capabilities`, and `crypto_provider_status` describe evidence supplied by an external crypto/TLS
  provider without implementing cryptographic primitives inside FlowQ.
- `packet_protector::provider_status()` defaults to unavailable, so existing test adapters and plaintext protection cannot
  accidentally satisfy `packet_protection_policy::production_required`.
- `production_required` now requires both `packet_security_level::authenticated_encrypted` and production-ready provider
  capabilities covering HKDF, AEAD seal/open, header protection, and TLS key-schedule ownership.
- `plaintext_packet_protector` remains `packet_security_level::test_only` and remains valid only under the default
  `test_allowed` policy used by deterministic tests and local smoke examples.

M28 does not add OpenSSL, BoringSSL, AWS-LC, Schannel, QuicTLS, TLS 1.3, AEAD, HKDF, header-protection implementation,
certificate validation, random generation, key schedule, packet-protection vectors, short headers, or interoperability
claims. It is a fail-closed boundary milestone only.

### M29 RFC 9001 Initial vector scope

M29 adds optional OpenSSL-backed helpers in `include/flowq/quic/initial_keys.hpp` for selected RFC 9001 Appendix A Initial
packet-protection vectors.

- The default build keeps the OpenSSL backend disabled and reports a structured error from Initial key helpers that require
  crypto primitives.
- Enabling `FLOWQ_ENABLE_OPENSSL_CRYPTO=ON` with the vcpkg manifest feature `openssl-crypto` links `OpenSSL::Crypto` and
  uses OpenSSL EVP APIs for HKDF-SHA256, AES-128-GCM seal/open, and AES-128-ECB header-protection sample masks.
- Vector tests verify the RFC 9001 Initial salt, initial/client/server secrets, client/server key material, the Appendix A
  header-protection mask sample, and AES-GCM authentication failure for altered AAD or ciphertext.
- The package config records `find_dependency(OpenSSL COMPONENTS Crypto)` only when FlowQ is configured with the OpenSSL
  backend enabled, so default package consumption stays backend-free. Consumers of an OpenSSL-enabled FlowQ install must
  make the same OpenSSL package discoverable through their toolchain, `CMAKE_PREFIX_PATH`, or `OPENSSL_ROOT_DIR`.

M29 does not implement TLS 1.3, certificate validation, a key schedule, complete QUIC packet protection, header-protection
integration, short headers, runtime provider selection, or interoperability. Passing selected Initial vectors is not a
production security claim.

### M30 transport parameter codec scope

M30 adds structural QUIC transport parameter support in `include/flowq/quic/transport_parameters.hpp` and maps selected
decoded values into `connection_loop_config` and `session_config`.

- `transport_parameters` models `max_idle_timeout`, `max_udp_payload_size`, `initial_max_data`, the three
  `initial_max_stream_data_*` parameters, `disable_active_migration`, `active_connection_id_limit`, and preserved unknown
  parameters.
- `encode_transport_parameters` and `decode_transport_parameters` use the existing QUIC varint helpers for deterministic
  TLV encoding and decoding.
- Decoding rejects duplicate parameters, truncated varints or values, non-empty `disable_active_migration`,
  `max_udp_payload_size < 1200`, and `active_connection_id_limit < 2` with structured codec errors. Encoding also rejects
  unencodable varint values, duplicate emitted parameter identifiers, and the same minimum-value violations instead of
  producing malformed payloads.
- `apply_transport_parameters` maps selected values into connection/session config, including connection flow-control
  credit, stream-credit config mirrors, idle timeout, UDP payload size, active CID limit, and active-migration disabling.
- Unknown parameters are preserved byte-for-byte so future negotiation layers can forward or inspect them without losing
  structural information.

M30 does not bind these parameters to TLS extensions, authenticate negotiation, validate peer role-specific semantics, add
real handshake state, or claim production interoperability. M31 owns the TLS handshake adapter and CRYPTO byte pump.

### M31 TLS handshake adapter boundary scope

M31 adds `include/flowq/quic/tls_handshake.hpp` as a narrow boundary between FlowQ's CRYPTO frames and a future external
QUIC-capable TLS provider.

- `tls_encryption_level`, `crypto_bytes`, `handshake_state`, and `tls_key_availability` describe opaque CRYPTO byte flow,
  adapter state, and key availability without exposing TLS transcript, certificate, key schedule, HKDF, AEAD, or random
  generation internals.
- `tls_handshake_adapter` accepts inbound CRYPTO bytes and drains outbound CRYPTO bytes; deterministic fake adapters live in
  unit tests only.
- `connection_loop` routes inbound CRYPTO frames to the adapter by packet space and pumps adapter-produced CRYPTO bytes into
  Initial, Handshake, or Application packet-space queues before flushing.
- Under `packet_protection_policy::production_required`, Application packets are blocked until the adapter reports
  `handshake_confirmed` and application key availability, in addition to the existing packet-protector/provider checks.
- `session_config` forwards the adapter pointer to the connection loop while keeping the public façade value-oriented.

M31 does not implement TLS 1.3, certificate validation, TLS transcript handling, key schedule, external provider wiring,
real packet-protection key installation, authenticated transport-parameter negotiation, or interoperability. M31b owns the
default-off external TLS provider adapter behind this boundary.
