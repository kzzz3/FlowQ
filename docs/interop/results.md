# FlowQ Interop Results

## Latest Results (2026-05-31)

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
| MsQuic | 2.4.8 | - | **PENDING** | Needs manual install |

### Environment

- **Platform**: Windows MSVC/vcpkg
- **FlowQ TLS**: OpenSSL QUIC TLS (OpenSSL 3.6.1)
- **Cipher Suite**: TLS_AES_128_GCM_SHA256

## Supported Peers

| Peer | Language | vcpkg | Status |
|------|----------|-------|--------|
| aioquic | Python | N/A | ✅ Verified |
| ngtcp2 | C | ✅ | ✅ Verified |
| MsQuic | C/C++ | ✅ | ⏳ Pending (BoringSSL conflict) |

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
| 2026-05-31 | aioquic + ngtcp2 | 5/5 | 0 |
| 2026-05-31 | aioquic | 3/3 | 0 |
