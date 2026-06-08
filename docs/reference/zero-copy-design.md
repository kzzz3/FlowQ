# FlowQ Zero-Copy Send Path

FlowQ implements a zero-copy send path that reduces buffer copies during packet construction and transmission.

## Components

### Zero-Copy Packet Builder

`zero_copy_packet_builder` constructs QUIC packets directly in the output buffer, avoiding intermediate copies. It integrates with the existing packet pipeline and supports both zero-copy and legacy paths.

### Protect-in-Place

`protect_in_place` performs AEAD encryption in the same buffer used for packet construction. This eliminates the copy from the assembly buffer to the encryption buffer.

### Datagram Buffer Pool

`datagram_buffer_pool` manages a pool of pre-allocated buffers for packet construction. Buffers are reused across packets to reduce allocation overhead.

### Enable Flag

The `enable_zero_copy` flag controls whether the zero-copy path is active. When disabled, the library falls back to the traditional multi-copy path with no performance penalty.

## Usage

Enable zero-copy via the session or endpoint configuration:

```cpp
// Zero-copy is controlled per-session
session_config config;
config.enable_zero_copy = true;
```

## Design Tradeoffs

- Zero-copy buffers are pinned during encryption, increasing peak memory usage slightly.
- The buffer pool pre-allocates memory, trading initial allocation cost for lower per-packet overhead.
- When zero-copy is disabled, the traditional path has identical behavior and correctness guarantees.
