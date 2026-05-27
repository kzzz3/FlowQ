# M2c Structural Frame Expansion Design

> **For agentic workers:** This is a design spec, not a full transport plan. Implement with TDD and keep the stage limited to byte-level frame encoding and decoding.

**Goal:** Extend the existing QUIC frame codec with structural support for ACK `0x02`, CRYPTO `0x06`, and STREAM `0x08..0x0f` frames.

**Architecture:** M2c extends `include/flowq/quic/frame.hpp` only. It reuses the existing varint codec and owned `flowq::buffer` values. It does not update protocol-core state, packet-number spaces, TLS, stream lifecycle, or flow-control logic.

**Tech Stack:** C++20, `flowq::buffer`, `flowq::error`, `flowq::quic::varint`, Catch2 tests, no ASIO/TLS/OpenSSL dependency.

---

## 1. Normative References

- RFC 9000 §12.4 Frames and Frame Types: https://www.rfc-editor.org/rfc/rfc9000.html#section-12.4
- RFC 9000 §16 Variable-Length Integer Encoding: https://www.rfc-editor.org/rfc/rfc9000.html#section-16
- RFC 9000 §19.3 ACK Frames: https://www.rfc-editor.org/rfc/rfc9000.html#section-19.3
- RFC 9000 §19.3.1 ACK Ranges: https://www.rfc-editor.org/rfc/rfc9000.html#section-19.3.1
- RFC 9000 §19.3.2 ECN Counts: https://www.rfc-editor.org/rfc/rfc9000.html#section-19.3.2
- RFC 9000 §19.6 CRYPTO Frames: https://www.rfc-editor.org/rfc/rfc9000.html#section-19.6
- RFC 9000 §19.8 STREAM Frames: https://www.rfc-editor.org/rfc/rfc9000.html#section-19.8

## 2. Scope

M2c adds structural frame variants:

1. ACK frame type `0x02` only.
2. CRYPTO frame type `0x06`.
3. STREAM frame type range `0x08..0x0f`.

M2c keeps ACK `0x03` with ECN counts unsupported. Unknown or unsupported frame types continue returning structured decode errors.

### Explicitly Out of Scope

- ACK semantics, RTT calculation, packet loss detection, or recovery.
- ACK ECN frame `0x03`.
- TLS interpretation of CRYPTO payload bytes.
- Packet protection, header protection, or encryption levels.
- Stream state machine, reassembly, final-size validation, flow control, or backpressure.
- Any integration with `flowq::quic::core` beyond future consumers being able to inspect decoded values.

## 3. Frame Value Types

Extend `flowq::quic::frame` with these variants:

```cpp
struct ack_range {
    std::uint64_t gap;
    std::uint64_t length;
};

struct ack_frame {
    std::uint64_t largest_acknowledged;
    std::uint64_t ack_delay;
    std::uint64_t first_ack_range;
    std::vector<ack_range> ranges;
};

struct crypto_frame {
    std::uint64_t offset;
    flowq::buffer data;
};

struct stream_frame {
    std::uint64_t stream_id;
    std::uint64_t offset;
    bool offset_present;
    bool length_present;
    bool fin;
    flowq::buffer data;
};
```

The `length_present` flag must be preserved because STREAM frames without `LEN` consume the remaining frame-buffer bytes. Re-encoding such a frame should preserve whether the input had an explicit length field.

## 4. Decode Rules

### 4.1 ACK `0x02`

Decode fields as varints:

1. Largest Acknowledged.
2. ACK Delay.
3. ACK Range Count.
4. First ACK Range.
5. Exactly `ACK Range Count` pairs of Gap and ACK Range Length.

M2c only checks structural completeness. It does not validate whether ranges are semantically meaningful or descending.

### 4.2 CRYPTO `0x06`

Decode fields as:

1. Offset varint.
2. Length varint.
3. Exactly Length bytes of opaque crypto data.

The payload is bytes only. No TLS parsing or handshake state changes happen in M2c.

### 4.3 STREAM `0x08..0x0f`

STREAM type bits:

- `0x04`: OFF bit, Offset field present.
- `0x02`: LEN bit, Length field present.
- `0x01`: FIN bit, final data marker.

Decode fields as:

1. Stream ID varint.
2. Offset varint only when OFF bit is set; otherwise offset is `0`.
3. Length varint only when LEN bit is set.
4. If LEN is set, read exactly Length data bytes.
5. If LEN is not set, consume all remaining bytes as stream data.

M2c must not interpret stream type, initiator, direction, final size, or flow-control effects.

## 5. Encode Rules

- ACK always encodes as type `0x02`.
- CRYPTO always encodes as type `0x06`.
- STREAM encodes type `0x08 | OFF | LEN | FIN` from `stream_frame` flags.
- STREAM with `offset_present == false` must encode offset `0` implicitly and omit the Offset field.
- STREAM with `length_present == false` must omit the Length field and place data as the final bytes of the encoded frame.
- All varints use the existing minimal encoder.

## 6. Error Handling

All malformed inputs return `flowq::error{flowq::error_code::protocol_error, message}`.

Required decode failures:

- ACK `0x03` unsupported.
- ACK Range Count larger than available Gap/Range pairs.
- Truncated ACK varint fields.
- Truncated CRYPTO Offset, Length, or data bytes.
- Truncated STREAM Stream ID, Offset, Length, or explicit-length data bytes.
- STREAM without LEN is valid and consumes the rest of the input buffer.

Do not throw exceptions for malformed frames.

## 7. Testing Strategy

Extend `tests/unit/quic_frame_tests.cpp`.

Required positive tests:

1. ACK `0x02` round-trips with zero additional ranges.
2. ACK `0x02` round-trips with at least one Gap/Range pair.
3. CRYPTO round-trips with offset and opaque payload.
4. STREAM round-trips for each type `0x08..0x0f`.
5. STREAM without LEN consumes the remaining input as data.
6. Mixed frame sequence decodes old and new frames together.

Required negative tests:

1. ACK `0x03` fails as unsupported.
2. ACK Range Count mismatch fails.
3. CRYPTO declared Length longer than available bytes fails.
4. STREAM explicit Length longer than available bytes fails.
5. Truncated nested varints in ACK, CRYPTO, or STREAM fail.

## 8. Acceptance Criteria

- Existing 34 tests continue passing.
- New tests cover ACK, CRYPTO, and all STREAM variants.
- `frame.hpp` remains header-only and dependency-light.
- Documentation states M2c is structural codec expansion only.
- No ASIO, TLS, OpenSSL, packet protection, loss recovery, stream state machine, or flow-control code is added.

## 9. Stop Point After M2c

After M2c, stop before packet protection, TLS, ACK/loss behavior, or stream state machine work. The next stage needs a new design boundary because it must choose between:

1. TLS/header-protection integration.
2. ACK/loss recovery semantics.
3. Stream state and flow-control semantics.

Those concerns should not be mixed into the frame codec.
