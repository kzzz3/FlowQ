# FlowQ Interop Results

## 2026-05-30 aioquic Handshake, Bidirectional Stream, And Loss-Recovery Evidence

- **FlowQ endpoint**: `build/windows-msvc-vcpkg-interop-openssl/Debug/flowq_quic_client.exe`
- **External peer**: Python `aioquic` 1.3.0 server from conda environment `expr`
- **Harness**: `tests/interop/test_interop.py`
- **TLS backend**: OpenSSL QUIC TLS (`OpenSSL 3.6.1 27 Jan 2026`)
- **Negotiated cipher suite**: `TLS_AES_128_GCM_SHA256`
- **Scope**: client Initial through TLS handshake confirmation, 1-RTT short-header packet protection, FlowQ STREAM data delivery, aioquic echo delivery back to FlowQ on stream 0, and application-space recovery after one intentionally dropped short-header datagram.
- **Result**: pass. `aioquic` observed `HandshakeCompleted` and stream 0 payload `hello from FlowQ`; FlowQ reported `Handshake confirmed`, `Recovery timer fired for packet number space 2; newly lost packets: 1`, `Retransmitting stream 0`, and received stream 0 payload `echo from aioquic`.
- **Latest recheck**: 2026-05-30 in conda environment `expr`; `bidirectional_stream` and `loss_recovery` both passed against aioquic 1.3.0.

## External QUIC Peer Availability

Checked on 2026-05-30:

- `ngtcp2*`: not found on PATH
- `quiche*`: not found on PATH
- `msquic*`: not found on PATH
- `picoquic*`: not found on PATH
- Docker CLI: present, Docker daemon not running in the current local environment
- WSL: installed, but no Linux distribution is available in the current local environment

## Open External Interop Results

- [x] `basic_handshake` against aioquic 1.3.0
- [x] FlowQ client STREAM delivery to aioquic 1.3.0
- [x] bidirectional stream echo against aioquic 1.3.0
- [x] `loss_recovery` against aioquic 1.3.0 with one intentionally dropped short-header datagram

## Next Steps

1. Add a second external peer once locally available.
