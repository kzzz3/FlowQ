# M4a Packet Assembly and Parsing Pipeline Design

## Goal

Integrate existing QUIC frame, packet-header, and recovery primitives into a small packet assembly/parsing layer. M4a
builds long-header packets from structured frames, parses long-header packets back into frames, enforces packet size
limits, and introduces a narrow packet-protection seam. It does not implement real TLS, AEAD, header protection,
short headers, stream state, or a full connection state machine.

## Why this stage

FlowQ currently has high-quality isolated pieces: varint, frames, long-header envelopes, ACK/loss, and recovery timing.
The next obvious flaw is that none of those pieces are connected into packet I/O. A basic QUIC implementation needs a
single path from connection intent to packet bytes and from packet bytes back to structured frame events.

## Non-goals

- No TLS 1.3 implementation, key schedule, AEAD, header protection, or certificate validation.
- No short-header packet support or packet-number reconstruction.
- No connection state machine beyond packet-number metadata on assembled/parsed packets.
- No stream lifecycle, reassembly, flow control, congestion control, or probe construction.
- No fake production crypto. Test doubles may transform bytes deterministically, but unprotected packets must be
  explicitly labelled as unprotected.

## Files

- Create `include/flowq/quic/packet_pipeline.hpp` for packet assembly/parsing value types and pure algorithms.
- Create `tests/unit/quic_packet_pipeline_tests.cpp` for builder/parser/protection-seam tests.
- Modify `tests/CMakeLists.txt` to include the new test file.
- Update `docs/development.md` with M4a scope and deferred behavior.

## Public API

```cpp
namespace flowq::quic {

enum class protection_level {
    none,
    initial,
    handshake,
    application
};

enum class long_packet_type {
    initial,
    handshake
};

struct packet_number {
    packet_number_space space{};
    std::uint64_t value{};
};

struct packet_pipeline_config {
    std::size_t max_datagram_size{1200};
};

struct packet_protection_result {
    flowq::buffer payload;
    flowq::error error{};
    bool ok() const noexcept;
};

class packet_protector {
public:
    virtual ~packet_protector() = default;
    virtual protection_level level() const noexcept = 0;
    virtual packet_protection_result protect(std::span<const std::byte> plaintext) const = 0;
    virtual packet_protection_result unprotect(std::span<const std::byte> protected_payload) const = 0;
};

class plaintext_packet_protector final : public packet_protector;

struct packet_build_request {
    long_packet_type type{};
    std::uint32_t version{1};
    connection_id destination_connection_id;
    connection_id source_connection_id;
    flowq::buffer token;
    packet_number number{};
    std::vector<frame> frames;
    const packet_protector* protector{};
    packet_pipeline_config config{};
};

struct assembled_packet {
    flowq::buffer datagram;
    packet_number number{};
    protection_level protection{};
};

struct parsed_packet {
    packet_header header;
    packet_number_space space{};
    protection_level protection{};
    std::vector<frame> frames;
};
```

M4a intentionally keeps packet number bytes outside the existing long-header codec. The current header codec treats
`protected_payload` as opaque. The pipeline therefore serializes packet number bytes at the front of the protected
payload and then frame bytes after them:

```text
protected_payload = packet_number_bytes || protected(frame_bytes)
```

M4a uses a fixed 4-byte packet number encoding for deterministic tests and simple future replacement. Future packet
protection/header-protection work can replace this with QUIC packet-number-length selection and reconstruction.

## Assembly behavior

`assemble_long_packet(request)`:

1. Requires a non-null `packet_protector`.
2. Rejects packet numbers larger than `0xffffffff`, because M4a uses fixed 4-byte packet numbers.
3. Requires packet number space to match packet type: Initial uses `packet_number_space::initial`; Handshake uses
   `packet_number_space::handshake`.
4. Requires protector level to match the packet type, while allowing `protection_level::none` for explicit plaintext
   test/dev packets.
5. Encodes each frame with `encode_frame` and concatenates frame bytes.
6. Calls `protector.protect(frame_bytes)`.
7. Prepends four big-endian packet-number bytes to the protected payload.
8. Builds an `initial_header` or `handshake_header` and calls `encode_packet_header`.
9. Rejects packets larger than `max_datagram_size`.
10. Returns `assembled_packet` with datagram, packet number metadata, and protection level.

For `long_packet_type::initial`, the request token is copied into the Initial header. For Handshake packets, token is
ignored.

## Parsing behavior

`parse_long_packet(datagram, protector)`:

1. Requires a non-null `packet_protector`.
2. Decodes the long-header envelope with `decode_packet_header`.
3. Accepts Initial and Handshake packets only.
4. Requires at least four packet-number bytes in the opaque protected payload.
5. Reads the first four bytes as the packet number for the corresponding packet number space.
6. Calls `protector.unprotect(remaining_payload)`.
7. Decodes frames with `decode_frames`.
8. Returns `parsed_packet` with header, space, protection level, and frames.

Version Negotiation, Retry, short headers, and unsupported long-header types remain outside M4a parsing.

## Crypto seam policy

`plaintext_packet_protector` is explicit and returns `protection_level::none`; it exists so structural packet pipeline
tests can run before TLS. Any protector that claims Initial/Handshake/Application protection must actually transform
bytes or be a test double named as such. Production code must not label plaintext as protected.

## Tests

Add deterministic tests for:

1. Initial packet assembly/parsing round trip containing ACK, CRYPTO, STREAM, and PADDING frames.
2. Handshake packet assembly/parsing round trip.
3. Builder rejects missing protector.
4. Parser rejects missing protector.
5. Builder rejects datagrams larger than `max_datagram_size`.
6. Parser rejects payloads shorter than the fixed 4-byte packet number.
7. Deterministic transforming test protector proves `protect` and `unprotect` are called.
8. Plaintext protector reports `protection_level::none`.
9. Builder rejects packet-number overflow, packet-type/space mismatch, and packet-type/protector-level mismatch.

## Acceptance criteria

- `cmake --build --preset windows-msvc-vcpkg` succeeds.
- `ctest --preset windows-msvc-vcpkg --timeout 10` passes all existing and new tests.
- M4a documentation clearly states that real TLS/AEAD/header protection, short headers, stream state, and connection
  state are deferred.
