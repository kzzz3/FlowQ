# FlowQ Release Checklist

Complete all items before claiming production-candidate status.

Strict gate:
```powershell
.\scripts\check-release-readiness.ps1 -RequireCompleteReleaseChecklist
```

## Build and Test

- [x] Windows MSVC/vcpkg: 511 tests passing
- [ ] Linux GCC/vcpkg: preset `linux-gcc-vcpkg` exists, evidence not recorded
- [x] Install + package-consumer builds and runs
- [x] No compiler warnings (`/W4 /WX` on MSVC)
- [ ] No sanitizer errors (ASan/UBSan): preset `linux-asan-ubsan` exists, evidence not recorded

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
- [x] Key material securely zeroed on destruction (secure.hpp)
- [x] Cipher-suite-aware header protection (AES-128/256-ECB, ChaCha20)
- [x] All protector types erase keys on destruction
- [x] traffic_secret() restricted to FLOWQ_ENABLE_INSPECTION

## Interop

- [x] aioquic 1.3.0 handshake PASS
- [x] aioquic 1.3.0 bidirectional stream echo PASS
- [x] aioquic 1.3.0 loss recovery PASS
- [x] Results recorded with peer name, version, TLS backend, cipher suite
- [ ] Second external peer interop PASS

## Congestion Control

- [x] NewReno congestion control
- [x] BBR congestion control
- [x] CUBIC congestion control (RFC 8312)
- [x] Pacing controller (RFC 9002 Section 7.7)

## Key Management

- [x] AEAD key rotation (RFC 9000 Section 6)
- [x] Key update state machine
- [x] Secure key material erasure on destruction

## Benchmarks

- [x] Benchmark framework established (40 scenarios)
- [x] Benchmark execution script (run-benchmarks.ps1)
- [x] Initial benchmark results recorded (9 scenarios PASS)
- [ ] Soak stability test (24 hours)
- [ ] Loss/reordering benchmark
- [ ] Migration benchmark

## Documentation

- [x] README.md status matches evidence
- [x] docs/plan.md describes current scope and gates
- [x] docs/milestones/roadmap.md maps evidence to gates
- [x] docs/production/readiness-gate.md matches evidence
- [x] docs/production/roadmap-to-production.md updated

## Human Review

- [ ] External security review
- [ ] Human reviewer sign-off
- [ ] Approval to change public wording
