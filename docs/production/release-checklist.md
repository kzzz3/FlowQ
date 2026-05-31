# FlowQ Release Checklist

Complete all items before claiming production-candidate status. Checked items must correspond to current evidence.

The strict production-candidate gate is:

```powershell
.\scripts\check-release-readiness.ps1 -RequireCompleteReleaseChecklist
```

Linux/macOS CI uses the matching POSIX smoke gate:

```bash
./scripts/check-release-readiness.sh --skip-build
```

`-SkipBuild` is only for local documentation/checklist smoke checks and must not be used as production-candidate evidence.

## Build and Test Gates

- [x] Full CTest passes on Windows MSVC/vcpkg preset
- [ ] Full CTest passes on Linux GCC/CMake preset
- [x] Install + package-consumer builds and runs
- [x] No compiler warnings with `-Wall -Wextra` (or MSVC equivalent)
- [ ] No sanitizer errors (ASAN, UBSAN)

Current Windows evidence: `scripts/validate-build.ps1 -Preset windows-msvc-vcpkg -BuildType Debug` passes configure, build, 481 CTest tests, install, and package-consumer configure/build/run on the Windows MSVC/vcpkg preset. Strict warning evidence: `FLOWQ_ENABLE_STRICT_WARNINGS=ON` Debug build passes with MSVC `/W4 /WX /permissive- /EHsc`, and `ctest --test-dir build/windows-msvc-vcpkg-warnings -C Debug --timeout 10 --output-on-failure` passes 481 CTest tests.

Linux GCC and sanitizer gates are defined but not checked off until they are executed on a Linux host: `linux-gcc-vcpkg`, `linux-gcc-vcpkg-strict`, and `linux-asan-ubsan` presets are available, `scripts/validate-build.sh --preset linux-gcc-vcpkg` validates CMake configure, build, CTest, install, package-consumer build, and package-consumer execution, and `scripts/validate-sanitizers.sh` validates ASan/UBSan. The current local Windows host cannot produce this evidence because no WSL distribution, Docker daemon, GCC, or Clang is available.

Code-quality evidence: `scripts/validate-checklist.ps1` and `scripts/validate-checklist.sh` check production public QUIC headers for TODO/FIXME comments, placeholder wording, type-safety suppressions, empty catch blocks, hardcoded credentials, documentation comments, and public API snake_case naming. `scripts/check-release-readiness.ps1 -RequireCompleteReleaseChecklist` and `scripts/check-release-readiness.sh --require-complete-release-checklist` fail while any required checklist item remains unchecked. CI runs the non-build readiness smoke gate after Windows, Linux, macOS, and sanitizer validation so release-gate regressions fail before checklist sign-off.

## Code Quality Gates

- [x] All public APIs have documentation comments
- [x] No `as any` or type safety suppressions
- [x] No empty catch blocks
- [x] No TODO/FIXME comments in production code paths
- [x] Consistent naming conventions across all modules

## Security Gates

- [x] No hardcoded keys, tokens, or credentials
- [x] No plaintext secrets in source or config files
- [x] Packet protection boundary keeps plaintext protection out of installed public headers and rejects non-provider-backed protectors
- [x] Installed package excludes HTTP/3, QPACK, 0-RTT, and test-support interop headers
- [x] TLS handshake adapter is boundary-only (no inline crypto)
- [x] No timing-sensitive code without constant-time annotations

## Interop Gates

- [x] aioquic 1.3.0 observes QUIC handshake completion from FlowQ
- [x] Python `bidirectional_stream` scenario passes against aioquic 1.3.0
- [x] Python `loss_recovery` scenario passes against aioquic 1.3.0
- [x] Interop results recorded with peer name, version, FlowQ TLS backend version, and negotiated cipher suite

## Documentation Gates

- [x] README.md status section matches actual evidence
- [x] docs/plan.md describes current production scope and gates
- [x] docs/milestones/roadmap.md maps current evidence to production gates
- [x] docs/production/readiness-gate.md status level matches evidence

## Scope Statement

Before claiming production-candidate:

1. Fill in the scope template in `docs/production/readiness-gate.md`
2. State exactly what is supported and what is not
3. Reference specific interop results
4. Reference TLS backend version and cipher suites

## Human Review

Production-ready status requires:

- [ ] External security review (not agent-generated)
- [ ] Sign-off from a human reviewer
- [ ] Explicit approval to change public wording
