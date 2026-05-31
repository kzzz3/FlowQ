# FlowQ Interop Results

## Latest Results

See `docs/interop/results/` for detailed JSON results.

## Supported Peers

| Peer | Language | vcpkg | Status |
|------|----------|-------|--------|
| aioquic | Python | N/A | ✅ Verified |
| MsQuic | C/C++ | ✅ | ⏳ Pending |
| ngtcp2 | C | ✅ | ⏳ Pending |
| lsquic | C | ✅ | ⏳ Pending |
| mvfst | C++ | ✅ | ⏳ Pending |

## Running Interop Tests

### All Peers

```powershell
# Build with interop support
cmake --preset windows-msvc-vcpkg -DFLOWQ_BUILD_INTEROP=ON
cmake --build --preset windows-msvc-vcpkg

# Run all interop tests
powershell -File scripts/run-interop-all.ps1
```

### Specific Peer

```powershell
# aioquic
conda run -n expr python tests/interop/test_interop.py

# MsQuic
powershell -File scripts/run-interop.ps1 -Peer msquic -Scenario basic_handshake

# ngtcp2
powershell -File scripts/run-interop.ps1 -Peer ngtcp2 -Scenario basic_handshake
```

## Scenarios

| Scenario | Description | Validation |
|----------|-------------|------------|
| basic_handshake | TLS handshake completion | HandshakeCompleted event |
| stream_echo | Bidirectional stream echo | Data correctly echoed |
| loss_recovery | Packet loss recovery | Retransmission and recovery |
| key_update | Key rotation | Key phase transition |

## Results Format

Results are stored in JSON format:

```json
{
  "date": "2026-05-31",
  "commit": "abc1234",
  "platform": "windows-msvc-vcpkg",
  "peers": [
    {"name": "aioquic", "version": "1.3.0"}
  ],
  "scenarios": [
    {
      "peer": "aioquic",
      "scenario": "basic_handshake",
      "result": "pass",
      "duration_ms": 123.45
    }
  ],
  "summary": {
    "total": 3,
    "passed": 3,
    "failed": 0,
    "errors": 0
  }
}
```

## Historical Results

| Date | Commit | Peers | Passed | Failed |
|------|--------|-------|--------|--------|
| 2026-05-31 | e16562b | aioquic | 3/3 | 0 |
