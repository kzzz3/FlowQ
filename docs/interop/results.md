# FlowQ Interop Results

## Latest Results (2026-06-06)

### Summary

| Metric | Value |
|--------|-------|
| Full-flow peers | 1 (aioquic) |
| Total scenarios | 2 |
| Passed | 2 |
| Failed | 0 |
| Machine report | `docs/interop/results/aioquic-20260606T153349Z-31934f6.json` |
| FlowQ commit | `31934f6` |
| Evidence validator | `scripts/validate-interop-evidence.py --results-dir docs/interop/results --min-full-flow-peers 1` |

### Peer Results

| Peer | Version | Scenario | Result | Details |
|------|---------|----------|--------|---------|
| aioquic | 1.3.0 | bidirectional_stream | PASS | Handshake + stream echo |
| aioquic | 1.3.0 | loss_recovery | PASS | Drop + retransmit + recovery |

### Environment

- **Platform**: Windows MSVC/vcpkg
- **FlowQ TLS**: OpenSSL QUIC TLS (OpenSSL 3.6.1)
- **Cipher Suite**: TLS_AES_128_GCM_SHA256
- **Client Config**: peer `127.0.0.1:4433`, stream payload `hello from FlowQ`, expected echo `echo from aioquic`
- **Runner**: `scripts/run-aioquic-interop.ps1 -CondaEnv expr -Scenario all`

## Machine Validation

```powershell
python scripts\validate-interop-evidence.py --results-dir docs\interop\results --min-full-flow-peers 1
```

The strict production-candidate gate uses `--min-full-flow-peers 2` and is expected to fail until a second external peer records the required full-flow scenarios.

## Supported Peers

| Peer | Language | Install | Status |
|------|----------|---------|--------|
| aioquic | Python | `pip install aioquic` | Verified full-flow peer |
| ngtcp2 | C | vcpkg | Optional Initial-packet smoke target |

## Running Interop Tests

```powershell
# Build with interop support
cmake --preset windows-msvc-vcpkg-interop-openssl
cmake --build --preset windows-msvc-vcpkg-interop-openssl --config Debug --target flowq_quic_client

# aioquic tests
.\scripts\run-aioquic-interop.ps1 -CondaEnv expr -Scenario all

# Validate checked-in evidence
python scripts\validate-interop-evidence.py --results-dir docs\interop\results --min-full-flow-peers 1

# Optional ngtcp2 Initial packet generation smoke
.\build\windows-msvc-vcpkg-interop-openssl\Debug\flowq_ngtcp2_initial_smoke.exe --ca build\certs\cert.pem
```

The interop preset enables the vcpkg `interop` manifest feature, which supplies ngtcp2 for the optional smoke target. The smoke target is structural only and does not count as second full-flow peer evidence.
