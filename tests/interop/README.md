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

## Evidence Validation

Checked-in JSON reports under `docs/interop/results` are validated by the release-readiness gate:

```powershell
python scripts\validate-interop-evidence.py --results-dir docs\interop\results --min-full-flow-peers 1
```

The strict production-candidate gate raises the peer minimum to 2 and remains blocked until a second full-flow external peer is recorded.

## Requirements

- `conda` must provide the `expr` environment with `aioquic` installed.
- `flowq_quic_client` must be built with OpenSSL QUIC TLS and OpenSSL crypto enabled.
- `FLOWQ_CLIENT`, `FLOWQ_INTEROP_SCENARIO`, and `FLOWQ_QUIC_EXPECT_ECHO` are set by `scripts/run-aioquic-interop.ps1`.
- `flowq_quic_client` reads peer and stream configuration from `FLOWQ_QUIC_PEER_HOST`, `FLOWQ_QUIC_PEER_PORT`, `FLOWQ_QUIC_STREAM_PAYLOAD`, and the required `FLOWQ_QUIC_EXPECT_ECHO` value.
- Missing binaries, missing conda/aioquic dependencies, unsupported scenarios, and non-zero scenario exits fail the production gate.
- Missing peer/version metadata, missing `flowq_commit`, failed required scenarios, and mismatched JSON summaries fail the release-readiness gate.
- Aioquic reports include `metadata.client_config` so peer host, peer port, stream payload, and expected echo settings are auditable with the evidence.

## Running

aioquic full-flow runner:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-aioquic-interop.ps1 -CondaEnv expr -Scenario all
```

Direct `flowq_quic_client` executions must set `FLOWQ_QUIC_EXPECT_ECHO` explicitly so the client validates the selected peer's response contract instead of assuming aioquic behavior.

Optional ngtcp2 Initial packet smoke:

```powershell
.\build\windows-msvc-vcpkg-interop-openssl\Debug\flowq_ngtcp2_initial_smoke.exe --ca build\certs\cert.pem
```
