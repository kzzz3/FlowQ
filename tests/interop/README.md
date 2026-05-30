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
- A selected scenario is provided with `FLOWQ_INTEROP_SCENARIO`
- FlowQ must be configured with a provider-backed TLS adapter for handshake and stream scenarios
- External peer runs should record the peer name, version, scenario, result, and harness output

## Running

```bash
ctest --test-dir build -C Debug -R interop --output-on-failure
```

PowerShell wrapper:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-interop.ps1 -Peer <peer-binary> -Scenario basic_handshake -BuildDir build\windows-msvc-vcpkg-interop
```

Bash wrapper:

```bash
./scripts/run-interop.sh --peer <peer-binary> --scenario basic_handshake --build-dir build/windows-msvc-vcpkg-interop
```
