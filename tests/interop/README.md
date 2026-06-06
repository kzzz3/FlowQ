# FlowQ Interop Tools

This directory contains the opt-in interop tools for testing FlowQ against mature QUIC implementations.

## Supported Peer Implementations

- aioquic full-flow server peer
- optional ngtcp2 Initial packet generation smoke target

## Usage

The C++ interop tools are opt-in and disabled by default. Enable them with:

```bash
cmake --preset windows-msvc-vcpkg-interop-openssl
```

## aioquic Scenarios

- `bidirectional_stream`: TLS handshake completion and bidirectional stream echo
- `loss_recovery`: One dropped short-header datagram, retransmission, and stream echo

## Requirements

- `conda` must provide the `expr` environment with `aioquic` installed.
- `flowq_quic_client` must be built with OpenSSL QUIC TLS and OpenSSL crypto enabled.
- `FLOWQ_CLIENT` and `FLOWQ_INTEROP_SCENARIO` are set by `scripts/run-aioquic-interop.ps1`.
- Missing binaries, missing conda/aioquic dependencies, unsupported scenarios, and non-zero scenario exits fail the production gate.

## Running

aioquic full-flow runner:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-aioquic-interop.ps1 -CondaEnv expr -Scenario all
```

Optional ngtcp2 Initial packet smoke:

```powershell
.\build\windows-msvc-vcpkg-interop-openssl\Debug\flowq_ngtcp2_interop.exe --ca build\certs\cert.pem
```
