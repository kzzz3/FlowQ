# FlowQ Release Checklist

Complete all items before claiming production-candidate status.

## Build and Test Gates

- [ ] Full CTest passes on Windows MSVC/vcpkg preset
- [ ] Full CTest passes on Linux GCC/CMake preset
- [ ] Install + package-consumer builds and runs
- [ ] No compiler warnings with `-Wall -Wextra` (or MSVC equivalent)
- [ ] No sanitizer errors (ASAN, UBSAN)

## Code Quality Gates

- [ ] All public APIs have documentation comments
- [ ] No `as any` or type safety suppressions
- [ ] No empty catch blocks
- [ ] No TODO/FIXME comments in production code paths
- [ ] Consistent naming conventions across all modules

## Security Gates

- [ ] No hardcoded keys, tokens, or credentials
- [ ] No plaintext secrets in source or config files
- [ ] Crypto provider boundary rejects test-only protectors when production policy is required
- [ ] TLS handshake adapter is boundary-only (no inline crypto)
- [ ] No timing-sensitive code without constant-time annotations

## Interop Gates

- [ ] basic_handshake scenario passes against at least one peer implementation
- [ ] stream_echo scenario passes against at least one peer implementation
- [ ] loss_recovery scenario passes against at least one peer implementation
- [ ] Interop results recorded with peer name, version, and FlowQ TLS backend version

## Documentation Gates

- [ ] README.md status section matches actual evidence
- [ ] PLAN.md roadmap checkboxes match actual milestone completion
- [ ] docs/basic-complete.md lists all installed headers
- [ ] docs/development.md documents all milestone scopes
- [ ] docs/production-readiness-gate.md status level matches evidence

## Scope Statement

Before claiming production-candidate:

1. Fill in the scope template in `docs/production-readiness-gate.md`
2. State exactly what is supported and what is not
3. Reference specific interop results
4. Reference TLS backend version and cipher suites

## Human Review

Production-ready status requires:

1. External security review (not agent-generated)
2. Sign-off from a human reviewer
3. Explicit approval to change public wording
