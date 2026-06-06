# FlowQ Release Checklist

Complete all items before claiming production-candidate status. Public production-ready or secure claims additionally require the human and audit gates below.

Strict gate:

```powershell
.\scripts\check-release-readiness.ps1 -RequireCompleteReleaseChecklist
```

The strict gate requires 2+ machine-validated full-flow external peers plus the human review and audit items below.

## Build and Test

- [x] Windows MSVC/vcpkg OpenSSL interop: 524/524 tests passing
- [x] Linux GCC/vcpkg: 510/511 tests passing (1 release_readiness not run)
- [x] Install + package-consumer builds and runs
- [x] No compiler warnings (`/W4 /WX` on MSVC)
- [x] No sanitizer errors (ASan/UBSan): 510/511 tests passing, 0 errors

## Code Quality

- [x] All public APIs have documentation comments
- [x] No type safety suppressions
- [x] No empty catch blocks
- [x] No TODO/FIXME in production code paths
- [x] No weak random generators in production QUIC headers
- [x] Consistent snake_case naming

## Security

- [x] No hardcoded keys, tokens, or credentials
- [x] No plaintext secrets in source or config
- [x] Packet protection boundary rejects non-provider-backed protectors
- [x] Installed package excludes HTTP/3, QPACK, 0-RTT, test-support headers
- [x] TLS handshake adapter is boundary-only
- [x] No timing-sensitive code without constant-time annotations
- [x] Key material zeroed on destruction
- [x] Cipher-suite-aware header protection (AES-128/256-ECB, ChaCha20)
- [x] All protector types erase keys on destruction
- [x] `traffic_secret()` restricted to `FLOWQ_ENABLE_INSPECTION`
- [ ] Human security review recorded
- [ ] External security audit recorded before public secure or production-ready claims

## Interop

- [x] aioquic 1.3.0 handshake PASS
- [x] aioquic 1.3.0 bidirectional stream echo PASS
- [x] aioquic 1.3.0 loss recovery PASS
- [x] Results recorded with peer name, version, TLS backend, cipher suite
- [x] Checked-in JSON evidence validated by `scripts/validate-interop-evidence.py`
- [ ] Second external peer full handshake and stream scenario PASS

## Congestion Control

- [x] NewReno congestion control
- [x] BBR congestion control
- [x] CUBIC congestion control (RFC 8312)
- [x] Pacing controller (RFC 9002 Section 7.7)

## Key Management

- [x] AEAD key rotation (RFC 9000 Section 6)
- [x] Key update state machine
- [x] Key material erasure on destruction

## Benchmarks

- [x] Benchmark framework established (40 scenarios)
- [x] Benchmark execution script (`run-benchmarks.ps1`)
- [x] Initial benchmark results recorded (9 scenarios PASS)
- [x] Soak stability test (10,000 connections, 0 errors)
- [x] Loss/reordering benchmark (8 scenarios, 4 PASS)
- [x] Migration benchmark (8 scenarios, 8 PASS)

## Documentation

- [x] README.md status matches evidence
- [x] docs/plan.md describes current scope and gates
- [x] docs/milestones/roadmap.md maps evidence to gates
- [x] docs/production/readiness-gate.md matches evidence
- [x] docs/production/roadmap-to-production.md updated
