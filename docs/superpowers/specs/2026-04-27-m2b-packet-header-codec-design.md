# M2b Packet Header Codec Design

> **For agentic workers:** This is a design spec, not an implementation plan. Before implementation, create a separate task plan and use TDD. Keep M2b bounded to structural packet-header codec work only.

**Goal:** Add a small, testable QUIC packet-header codec that parses and emits cleartext long-header envelopes without entering TLS, header protection, packet number reconstruction, or frame processing.

**Architecture:** M2b sits above the M2a varint/frame byte codecs and below the M1.5 protocol-core seam. It exposes pure C++20 value types and deterministic parse/encode functions. Packet payload, packet number bytes, and protected short-header fields remain opaque until later crypto/header-protection stages.

**Tech Stack:** C++20, existing `flowq::buffer`, `flowq::error`, `flowq::quic::varint`, Catch2 tests, no ASIO/TLS/OpenSSL dependency.

---

## 1. Normative References

- RFC 9000 §17.2 Long Header Packets: https://www.rfc-editor.org/rfc/rfc9000.html#section-17.2
- RFC 9000 §17.2.1 Version Negotiation: https://www.rfc-editor.org/rfc/rfc9000.html#section-17.2.1
- RFC 9000 §17.2.2 Initial: https://www.rfc-editor.org/rfc/rfc9000.html#section-17.2.2
- RFC 9000 §17.2.4 Handshake: https://www.rfc-editor.org/rfc/rfc9000.html#section-17.2.4
- RFC 9000 §17.2.5 Retry: https://www.rfc-editor.org/rfc/rfc9000.html#section-17.2.5
- RFC 9000 §17.3 Short Header Packets: https://www.rfc-editor.org/rfc/rfc9000.html#section-17.3
- RFC 9000 §17.1 Packet Number Encoding and Decoding: https://www.rfc-editor.org/rfc/rfc9000.html#section-17.1
- RFC 9000 §5.1 Connection ID: https://www.rfc-editor.org/rfc/rfc9000.html#section-5.1
- RFC 9001 §5.4 Header Protection: https://www.rfc-editor.org/rfc/rfc9001.html#section-5.4

## 2. M2b Scope

M2b implements a structural packet-header codec for these packet envelopes:

1. Version Negotiation packet.
2. Initial packet.
3. Handshake packet.
4. Retry packet.

M2b must recognize short-header packets and return a structured unsupported error. It must not parse short-header fields or preserve them as a supported packet variant.

### Explicitly Out of Scope

- Header protection removal or application.
- Packet number reconstruction.
- Packet number encode/decode semantics beyond preserving opaque packet-number bytes.
- Short-header key phase, reserved bits, and packet-number length interpretation.
- TLS handshake, CRYPTO frame integration, packet protection, and decryption.
- ACK, loss recovery, congestion control, stream semantics, and frame parsing inside packet payloads.
- Retry integrity-tag verification.

These exclusions are intentional. Packet headers are the first boundary where QUIC transport and TLS begin to overlap. M2b should prove structural parsing and serialization without pretending crypto exists.

## 3. Packet Rules to Model

### 3.1 Long Header Envelope

Long headers have the header form bit set and carry these common cleartext fields:

- First byte.
- Version, 4 bytes.
- Destination Connection ID length and bytes.
- Source Connection ID length and bytes.
- Packet-type-specific fields.

M2b should expose the decoded first byte for round-trip preservation, but only derive the safe long-header packet kind from unprotected fields and the long-header type bits. It must not rely on protected bits for behavior that belongs to a later header-protection stage.

### 3.2 Version Negotiation

Version Negotiation is identified by a long-header packet with `Version = 0`.

M2b should parse:

- First byte.
- Version = `0`.
- Destination Connection ID.
- Source Connection ID.
- Supported versions as a sequence of 4-byte unsigned integers.

Validation:

- DCID and SCID lengths must fit the input.
- Remaining bytes after CIDs must be a multiple of 4.
- Version list must be preserved in order.

### 3.3 Initial

Initial is a long-header packet with a token field and a length field.

M2b should parse:

- Common long-header fields.
- Token length as QUIC varint.
- Token bytes as owned `flowq::buffer`.
- Length as QUIC varint.
- Opaque protected packet bytes whose size is exactly `Length`.

The opaque protected packet bytes include packet number bytes and encrypted payload. M2b must not split them.

Validation:

- Token length varint must be complete.
- Token bytes must fit the input.
- Length varint must be complete.
- Opaque protected packet bytes must exactly fit the declared length.

### 3.4 Handshake

Handshake is a long-header packet without a token field.

M2b should parse:

- Common long-header fields.
- Length as QUIC varint.
- Opaque protected packet bytes whose size is exactly `Length`.

Validation mirrors Initial except there is no token.

### 3.5 Retry

Retry is a special long-header packet with no packet number and no Length field.

M2b should parse:

- Common long-header fields.
- Retry token and integrity tag as an opaque tail.

M2b must preserve the tail bytes, but it must not verify the integrity tag. Later crypto work can split token and integrity tag if needed.

Validation:

- DCID and SCID lengths must fit the input.
- Empty Retry tails are rejected because a real Retry needs token and integrity bytes.

### 3.6 Short Header

Short headers require connection state to know Destination Connection ID length and require header protection removal before interpreting key phase, reserved bits, and packet-number length.

M2b behavior:

- Detect header form bit = short header.
- Return a structured unsupported error.
- Do not add an `opaque_short_header` variant in M2b.

## 4. Public API Shape

Create `include/flowq/quic/packet_header.hpp`.

Required value types:

```cpp
namespace flowq::quic {

enum class packet_header_kind {
    version_negotiation,
    initial,
    handshake,
    retry
};

struct connection_id {
    flowq::buffer bytes;
};

struct version_negotiation_header {
    std::byte first_byte;
    connection_id destination_connection_id;
    connection_id source_connection_id;
    std::vector<std::uint32_t> supported_versions;
};

struct initial_header {
    std::byte first_byte;
    std::uint32_t version;
    connection_id destination_connection_id;
    connection_id source_connection_id;
    flowq::buffer token;
    std::uint64_t length;
    flowq::buffer protected_payload;
};

struct handshake_header {
    std::byte first_byte;
    std::uint32_t version;
    connection_id destination_connection_id;
    connection_id source_connection_id;
    std::uint64_t length;
    flowq::buffer protected_payload;
};

struct retry_header {
    std::byte first_byte;
    std::uint32_t version;
    connection_id destination_connection_id;
    connection_id source_connection_id;
    flowq::buffer opaque_retry_tail;
};

using packet_header = std::variant<
    version_negotiation_header,
    initial_header,
    handshake_header,
    retry_header
>;

struct packet_header_decode_result {
    packet_header header;
    flowq::error error;
    bool ok() const noexcept;
};

struct packet_header_encode_result {
    flowq::buffer payload;
    flowq::error error;
    bool ok() const noexcept;
};

packet_header_decode_result decode_packet_header(std::span<const std::byte> input);
packet_header_decode_result decode_packet_header(const flowq::buffer& input);
packet_header_encode_result encode_packet_header(const packet_header& header);

} // namespace flowq::quic
```

These names and boundaries are part of the M2b contract:

- Inputs and outputs are owned values.
- Parse functions are synchronous and deterministic.
- Errors use `flowq::error` with `flowq::error_code::protocol_error`.
- No ASIO, TLS, OpenSSL, stdexec, or socket headers are included.

## 5. Data Flow

```text
UDP datagram bytes
  -> decode_packet_header(bytes)
  -> packet_header value OR flowq::error
  -> future protocol-core state machine
```

Encoding follows the reverse path:

```text
packet_header value
  -> encode_packet_header(header)
  -> owned flowq::buffer bytes
  -> future ASIO sender sends bytes
```

M2b does not pass decoded frames to the protocol core. The protected payload remains opaque because packet protection and frame decryption are later stages.

## 6. Error Handling

All malformed inputs return `flowq::error{flowq::error_code::protocol_error, message}`.

Required error cases:

- Empty input.
- Short header unsupported.
- Truncated common long-header fields.
- DCID or SCID length exceeds remaining input.
- Version Negotiation version list length is not divisible by 4.
- Initial token length varint is truncated.
- Initial token bytes are truncated.
- Initial or Handshake Length varint is truncated.
- Initial or Handshake protected payload length does not match declared Length.
- Retry tail is empty.
- Unknown long-header packet type.

Do not throw exceptions for malformed packets.

## 7. Testing Strategy

Create `tests/unit/quic_packet_header_tests.cpp`.

Required positive tests:

1. Decode Version Negotiation with two supported versions and assert DCID/SCID/version order.
2. Encode Version Negotiation and decode it back.
3. Decode Initial with token, Length, and opaque protected payload.
4. Encode Initial and decode it back.
5. Decode Handshake with Length and opaque protected payload.
6. Decode Retry with opaque tail.

Required negative tests:

1. Empty input fails.
2. Short header fails as unsupported.
3. Truncated version field fails.
4. Truncated CID length or CID bytes fail.
5. Version Negotiation version list with 1, 2, or 3 trailing bytes fails.
6. Initial token length varint truncation fails.
7. Initial/Handshake declared Length longer than remaining bytes fails.
8. Retry with no tail fails.
9. Unknown long-header packet type fails.

Testing must avoid TLS vectors and encrypted packet contents. Opaque payload bytes can be small fixed byte arrays.

## 8. Acceptance Criteria

- `packet_header.hpp` compiles without ASIO, TLS, OpenSSL, stdexec, or socket includes.
- Positive and negative tests pass with CTest.
- Existing M0/M1/M1.5/M2a tests continue passing.
- Documentation states short headers and header protection are out of scope.
- Packet number bytes remain opaque and are not reconstructed.
- Version Negotiation, Initial, Handshake, and Retry are structurally parsed and encoded as owned values.

## 9. Follow-up After M2b

The next stage after M2b should be chosen explicitly:

1. **M2c packet-number/header-protection design** if the project is ready to introduce TLS/header protection boundaries.
2. **M2c ACK/STREAM/CRYPTO frame expansion** if packet headers remain structural and the codec layer needs more frame coverage first.

Do not implement either path until its boundary is written down. Both paths can easily pull in TLS, packet protection, and state-machine semantics if left implicit.
