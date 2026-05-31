# FlowQ Interop Results

## Latest Results (2026-06-01)

### Summary

| Metric | Value |
|--------|-------|
| Verified Peers | 2 (aioquic, ngtcp2) |
| Total Scenarios | 5 |
| Passed | 5 |
| Failed | 0 |

### Peer Results

| Peer | Version | Scenario | Result | Details |
|------|---------|----------|--------|---------|
| aioquic | 1.3.0 | bidirectional_stream | **PASS** | Handshake + stream echo |
| aioquic | 1.3.0 | loss_recovery | **PASS** | Drop + retransmit + recovery |
| ngtcp2 | 1.20.0 | initial_packet | **PASS** | Initial packet generation |

### Environment

- **Platform**: Windows MSVC/vcpkg
- **FlowQ TLS**: OpenSSL QUIC TLS (OpenSSL 3.6.1)
- **Cipher Suite**: TLS_AES_128_GCM_SHA256

## Supported Peers

| Peer | Language | Install | Status | Notes |
|------|----------|---------|--------|-------|
| aioquic | Python | `pip install aioquic` | ✅ Verified | Full handshake + stream + loss recovery |
| ngtcp2 | C | vcpkg | ✅ Verified | Initial packet generation |
| quic-go | Go | `go build` | ⚠️ Built | Binary built, needs separate terminal for server |
| quiche | Rust | `cargo build` | ❌ Blocked | Requires NASM for BoringSSL on Windows |

## Building quic-go

```powershell
# Clone and build
git clone --depth 1 https://github.com/quic-go/quic-go.git tools/quic-go
cd tools/quic-go
go build -o ..\quic-go-client.exe .\example\client\main.go
go build -o ..\quic-go-server.exe .\example\main.go

# Run server (in separate terminal)
.\quic-go-server.exe --cert build\certs\cert.pem --key build\certs\key.pem --bind :4434

# Run client
.\quic-go-client.exe --cert build\certs\cert.pem https://localhost:4434/
```

## Building quiche

```powershell
# Requires NASM (https://www.nasm.us/)
# Install NASM first, then:
git clone --depth 1 https://github.com/cloudflare/quiche.git tools/quiche
cd tools/quiche
cargo build --examples --release
```

## Running Interop Tests

```powershell
# Build with interop support
cmake --preset windows-msvc-vcpkg-interop -DVCPKG_MANIFEST_FEATURES="interop"
cmake --build --preset windows-msvc-vcpkg-interop --config Debug

# aioquic tests
conda run -n expr python tests/interop/test_interop.py

# ngtcp2 interop
.\build\windows-msvc-vcpkg-interop-openssl\Debug\flowq_ngtcp2_interop.exe --ca build\certs\cert.pem
```

## Historical Results

| Date | Peers | Passed | Failed |
|------|-------|--------|--------|
| 2026-06-01 | aioquic + ngtcp2 | 5/5 | 0 |
| 2026-05-31 | aioquic + ngtcp2 | 5/5 | 0 |
