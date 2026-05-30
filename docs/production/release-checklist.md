# FlowQ Release Checklist

Complete all items before claiming production-candidate status. Checked items must correspond to current evidence, not historical intent.

## Build and Test Gates

- [x] Full CTest passes on Windows MSVC/vcpkg preset
- [ ] Full CTest passes on Linux GCC/CMake preset
- [x] Install + package-consumer builds and runs
- [x] No compiler warnings with `-Wall -Wextra` (or MSVC equivalent)
- [ ] No sanitizer errors (ASAN, UBSAN)

Current Windows evidence: `scripts/validate-build.ps1` passes configure, build, 482 CTest tests, install, and package-consumer configure/build on the Windows MSVC/vcpkg preset; `build/package-consumer/Debug/flowq_package_consumer.exe` exits successfully. Strict warning evidence: `FLOWQ_ENABLE_STRICT_WARNINGS=ON` Debug build passes with MSVC `/W4 /WX /permissive- /EHsc`, and the strict-warning build runs 483 CTest tests successfully.

Linux GCC and sanitizer gates are defined but not checked off until they are executed on a Linux host: `linux-gcc-vcpkg`, `linux-gcc-vcpkg-strict`, and `linux-asan-ubsan` presets are available, `scripts/validate-build.sh --preset linux-gcc-vcpkg` validates the Linux package pipeline, and `scripts/validate-sanitizers.sh` validates ASan/UBSan. The current local Windows host cannot produce this evidence because no WSL distribution, Docker daemon, GCC, or Clang is available.

## Code Quality Gates

- [x] All public APIs have documentation comments
- [x] No `as any` or type safety suppressions
- [x] No empty catch blocks
- [x] No TODO/FIXME comments in production code paths
- [ ] Consistent naming conventions across all modules

## Security Gates

- [x] No hardcoded keys, tokens, or credentials
- [x] No plaintext secrets in source or config files
- [x] Crypto provider boundary rejects test-only protectors when production policy is required
- [x] TLS handshake adapter is boundary-only (no inline crypto)
- [x] No timing-sensitive code without constant-time annotations

## Interop Gates

- [x] basic_handshake scenario passes against aioquic 1.3.0
- [x] stream_echo scenario passes against aioquic 1.3.0
- [x] loss_recovery scenario passes against aioquic 1.3.0
- [x] Interop results recorded with peer name, version, FlowQ TLS backend version, and negotiated cipher suite

## Documentation Gates

- [x] README.md status section matches actual evidence
- [x] docs/plan.md roadmap checkboxes match actual milestone completion
- [x] docs/milestones/roadmap.md documents all milestone scopes
- [x] docs/production/readiness-gate.md status level matches evidence

## Scope Statement

Before claiming production-candidate:

1. Fill in the scope template in `docs/production/readiness-gate.md`
2. State exactly what is supported and what is not
3. Reference specific interop results
4. Reference TLS backend version and cipher suites

## Human Review

Production-ready status requires:

1. External security review (not agent-generated)
2. Sign-off from a human reviewer
3. Explicit approval to change public wording
