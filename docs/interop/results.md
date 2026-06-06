# FlowQ Interop Results

## Latest Results (2026-06-01)

### Summary

| Metric | Value |
|--------|-------|
| Full-flow peers | 1 (aioquic) |
| Structural smoke peers | 1 (ngtcp2) |
| Total scenarios | 3 |
| Passed | 3 |
| Failed | 0 |

### Peer Results

| Peer | Version | Scenario | Result | Details |
|------|---------|----------|--------|---------|
| aioquic | 1.3.0 | bidirectional_stream | PASS | Handshake + stream echo |
| aioquic | 1.3.0 | loss_recovery | PASS | Drop + retransmit + recovery |
| ngtcp2 | 1.20.0 | initial_packet | PASS | Initial packet generation smoke |

### Environment

- **Platform**: Windows MSVC/vcpkg
- **FlowQ TLS**: OpenSSL QUIC TLS (OpenSSL 3.6.1)
- **Cipher Suite**: TLS_AES_128_GCM_SHA256

## Supported Peers

| Peer | Language | Install | Status |
|------|----------|---------|--------|
| aioquic | Python | `pip install aioquic` | Verified full-flow peer |
| ngtcp2 | C | vcpkg | Verified Initial-packet smoke peer |

## Running Interop Tests

```powershell
# Build with interop support
cmake --preset windows-msvc-vcpkg-interop -DVCPKG_MANIFEST_FEATURES="interop"
cmake --build --preset windows-msvc-vcpkg-interop --config Debug

# aioquic tests
conda run -n expr python tests/interop/test_interop.py

# ngtcp2 Initial packet generation smoke
.\build\windows-msvc-vcpkg-interop-openssl\Debug\flowq_ngtcp2_interop.exe --ca build\certs\cert.pem
```
