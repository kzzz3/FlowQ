# FlowQ Interop Results

## 2026-05-30 aioquic Handshake And Bidirectional Stream Evidence

- **FlowQ endpoint**: `build/windows-msvc-vcpkg-interop-openssl/Debug/flowq_quic_client.exe`
- **External peer**: Python `aioquic` 1.3.0 server from conda environment `expr`
- **Harness**: `tests/interop/test_interop.py`
- **TLS backend**: OpenSSL QUIC TLS (`OpenSSL 3.6.1 27 Jan 2026`)
- **Negotiated cipher suite**: `TLS_AES_128_GCM_SHA256`
- **Scope**: client Initial through TLS handshake confirmation, 1-RTT short-header packet protection, FlowQ STREAM data delivery, and aioquic echo delivery back to FlowQ on stream 0.
- **Result**: pass. `aioquic` observed `HandshakeCompleted` and stream 0 payload `hello from FlowQ`; FlowQ reported `Handshake confirmed` and received stream 0 payload `echo from aioquic`.

## External QUIC Peer Availability

Checked on 2026-05-30:

- `ngtcp2*`: not found on PATH
- `quiche*`: not found on PATH
- `msquic*`: not found on PATH
- `picoquic*`: not found on PATH
- Docker CLI: present, Docker daemon running
- WSL: available for testing

## Open External Interop Results

- [x] `basic_handshake` against aioquic 1.3.0
- [x] FlowQ client STREAM delivery to aioquic 1.3.0
- [x] bidirectional stream echo against aioquic 1.3.0
- [ ] `loss_recovery` against a named external QUIC peer and version

## Next Steps

1. Add an external loss-recovery scenario with packet loss evidence.
2. Add a second external peer once locally available.
