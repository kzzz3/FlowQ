# FlowQ Interop Harness

This directory contains an opt-in interop harness for testing FlowQ against mature QUIC implementations.

## Supported Peer Implementations

- ngtcp2
- quiche (Cloudflare)
- MsQuic (Microsoft)
- picoquic
- lsquic

## Usage

The interop harness is opt-in and disabled by default. Enable it with:

```bash
cmake -S . -B build -DFLOWQ_BUILD_INTEROP=ON
```

## Scenarios

- `basic_handshake.json`: TLS handshake completion and connection establishment
- `stream_echo.json`: Bidirectional stream data exchange
- `loss_recovery.json`: Packet loss recovery and retransmission

## Requirements

- Peer QUIC binary must be available in PATH or specified via `FLOWQ_INTEROP_PEER_BIN`
- FlowQ must be configured with a provider-backed TLS adapter (M31b) for handshake/stream scenarios
- Without a TLS adapter, handshake/stream scenarios are skipped with an explicit message

## Running

```bash
ctest --test-dir build -C Debug -R interop --output-on-failure
```
