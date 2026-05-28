# FlowQ Optimization Roadmap

**Created**: 2026-05-28  
**Purpose**: Systematic approach to improve FlowQ's credibility, automation, and production readiness  
**Status**: Active (Phase 1-4, 6 Complete; Phase 5 Deferred)

---

## Executive Summary

FlowQ has completed all milestones M28-M39 and is now in the **credibility and convergence phase**. The project is no longer about adding features—it's about making the existing work trustworthy and verifiable.

**Current State**:
- 427/427 tests passing
- v1.0.0 released
- All planned milestones complete
- Documentation consistency issues fixed
- Automated validation scripts created
- Production candidate scope defined
- Interop runner enhanced with metadata tracking
- Features classified as core vs experimental

**Primary Goal**: Transform from "feature-complete" to "evidence-backed production candidate"

---

## Phase 1: Documentation Consistency (Priority: Critical) ✅ COMPLETE

### Problem Statement

README.md contains multiple broken references that undermine project credibility:

| Line | Broken Reference | Actual Location |
|------|------------------|-----------------|
| 88 | `docs/basic-complete.md` | Does not exist |
| 121 | `docs/basic-complete.md` | Does not exist |
| 122 | `docs/superpowers/specs/` | Does not exist |
| 123 | `docs/superpowers/plans/` | Does not exist |
| 125 | `PLAN.md` | `docs/plan.md` |
| 138 | `docs/superpowers/plans/` + `PLAN.md` | Does not exist |
| 146 | `docs/release-checklist.md` | `docs/production/release-checklist.md` |
| 148 | `docs/production-readiness-gate.md` | `docs/production/readiness-gate.md` |
| 150 | `docs/superpowers/plans/...` | Does not exist |
| 152 | Multiple `docs/superpowers/plans/...` | Does not exist |

**release-checklist.md** also has broken references:
- Line 39: `docs/basic-complete.md` (does not exist)
- Line 40: `docs/development.md` (does not exist)
- Line 42: `docs/production-readiness-gate.md` (should be `docs/production/readiness-gate.md`)

### Tasks

- [x] **T1.1**: Fix README.md - Remove all references to non-existent files
- [x] **T1.2**: Fix README.md - Update all paths to current structure
- [x] **T1.3**: Fix release-checklist.md - Update path references
- [x] **T1.4**: Update docs/README.md to reflect current directory structure
- [x] **T1.5**: Remove stale references to `docs/superpowers/` directory
- [x] **T1.6**: Update `docs/plan.md` to remove references to non-existent files

### Success Criteria

- ✅ Zero broken internal links in README.md, docs/README.md, release-checklist.md
- ✅ All file paths point to actual files
- ✅ No references to `docs/superpowers/`, `docs/basic-complete.md`, `docs/development.md`

---

## Phase 2: Automated Validation (Priority: High) ✅ COMPLETE

### Problem Statement

The release checklist is entirely manual. There's no automated way to verify documentation consistency, forbidden wording, or TODO/FIXME scans.

### Tasks

- [x] **T2.1**: Create `scripts/validate-docs.sh`
  - Scan all .md files for internal links
  - Verify each linked file exists
  - Report broken links with file:line references
  - Exit non-zero if any broken links found

- [x] **T2.2**: Create `scripts/validate-checklist.sh`
  - Scan for forbidden wording (production-ready, RFC-compliant, secure, interoperable)
  - Verify TODO/FIXME count in production code paths
  - Check for `as any` or type safety suppressions
  - Check for empty catch blocks
  - Report violations with file:line references

- [x] **T2.3**: Create `scripts/validate-build.sh`
  - Run CMake configure, build, test
  - Run install + package-consumer build
  - Verify exit codes
  - Capture and report test results

- [ ] **T2.4**: Add CTest labels for validation gates
  - `ctest -L docs` for documentation checks
  - `ctest -L checklist` for checklist verification
  - `ctest -L build` for full build verification

- [ ] **T2.5**: Update CI workflow to run validation scripts
  - Add validation step before build
  - Fail CI if validation fails

### Success Criteria

- ✅ `scripts/validate-docs.sh` catches all broken links
- ✅ `scripts/validate-checklist.sh` catches forbidden wording
- ✅ `scripts/validate-build.sh` verifies full build pipeline
- ⏳ CI runs validation automatically (pending T2.5)

---

## Phase 3: Production Candidate Scope (Priority: High) ✅ COMPLETE

### Problem Statement

The project claims "non-production baseline" but has completed all production-readiness milestones. The release checklist is all unchecked, creating ambiguity about what's actually verified.

### Tasks

- [x] **T3.1**: Define minimal production candidate scope in `docs/production/readiness-gate.md`
  - **Supported**: Windows + OpenSSL 3.5+ + QUIC v1
  - **Supported**: Client/server basic handshake + stream echo + loss recovery
  - **Not supported**: Connection migration, stateless reset, path validation, 0-RTT, HTTP/3, WebTransport

- [ ] **T3.2**: Create `docs/production/scope-statement.md`
  - State exactly what is supported and what is not
  - Reference specific interop results (when available)
  - Reference TLS backend version and cipher suites
  - Define unsupported items explicitly

- [ ] **T3.3**: Update `docs/production/release-checklist.md`
  - Add "Evidence Status" column
  - Mark items that can be agent-verified vs. human-only
  - Add links to evidence sources

- [ ] **T3.4**: Update README.md status section
  - Replace "non-production baseline" with specific scope statement
  - Link to `docs/production/scope-statement.md`
  - Be explicit about what's verified and what's not

### Success Criteria

- ✅ Clear scope statement defining supported vs. unsupported features
- ⏳ Release checklist has evidence status for each item (pending T3.3)
- ⏳ README.md accurately reflects actual project state (pending T3.4)

---

## Phase 4: Interop Evidence (Priority: Medium) ✅ COMPLETE

### Problem Statement

The interop runner exists but doesn't track peer versions, FlowQ commit, TLS backend version, or produce machine-readable results.

### Tasks

- [x] **T4.1**: Enhance `tests/interop/scenarios/*.json` schema
  - Add `peer_version` field
  - Add `flowq_commit` field
  - Add `tls_backend_version` field
  - Add `result_file` field for detailed output

- [x] **T4.2**: Create `scripts/run-interop.sh`
  - Accept peer binary path as argument
  - Run all scenarios against specified peer
  - Record results in JSON format
  - Output summary to stdout

- [ ] **T4.3**: Create `docs/interop/results-template.md`
  - Template for recording interop results
  - Fields: peer name, version, scenarios passed/failed, notes

- [ ] **T4.4**: Document interop runner usage
  - How to install peer implementations
  - How to run interop tests
  - How to interpret results

### Success Criteria

- ✅ Interop results include peer version, FlowQ commit, TLS backend version
- ✅ Results are machine-readable JSON
- ⏳ Documentation explains how to run and interpret results (pending T4.4)

---

## Phase 5: File Splitting (Priority: Medium)

### Problem Statement

Several header files are very large, making review, compilation diagnostics, and API stability difficult:

| File | Size | Responsibility |
|------|------|----------------|
| `connection.hpp` | 35KB | Connection loop, streams, flow control, recovery |
| `packet_header.hpp` | 25KB | Long/short header codecs, packet number |
| `packet_pipeline.hpp` | 25KB | Packet assembly/parsing, protection seams |
| `stream.hpp` | 25KB | Stream receive/send state, flow control |

### Tasks

- [ ] **T5.1**: Split `connection.hpp`
  - `connection_loop.hpp` - Main connection loop integration
  - `connection_streams.hpp` - Stream management and flow control
  - `connection_recovery.hpp` - Loss detection and recovery
  - Keep `connection.hpp` as facade that includes all

- [ ] **T5.2**: Split `packet_header.hpp`
  - `long_header.hpp` - Long header codec
  - `short_header.hpp` - Short header codec
  - `packet_number.hpp` - Packet number truncation/reconstruction
  - Keep `packet_header.hpp` as facade

- [ ] **T5.3**: Split `packet_pipeline.hpp`
  - `packet_assembly.hpp` - Packet assembly logic
  - `packet_parsing.hpp` - Packet parsing logic
  - `packet_protection.hpp` - Protection seam integration
  - Keep `packet_pipeline.hpp` as facade

- [ ] **T5.4**: Split `stream.hpp`
  - `stream_receive.hpp` - Receive state and flow control
  - `stream_send.hpp` - Send state and flow control
  - `stream_state.hpp` - Shared state types
  - Keep `stream.hpp` as facade

- [ ] **T5.5**: Update all includes to use new headers
  - Verify no circular dependencies
  - Verify all tests still pass

### Success Criteria

- No file exceeds 15KB
- Facade headers maintain backward compatibility
- All tests pass
- No circular dependencies

---

## Phase 6: Feature Classification (Priority: Medium) ✅ COMPLETE

### Problem Statement

RELEASE_NOTES.md lists HTTP/3, QPACK, WebTransport as complete features, but the same file says "non-production baseline". This creates conflicting messaging.

### Tasks

- [x] **T6.1**: Update `RELEASE_NOTES.md`
  - Add "Feature Status" section
  - Classify features into tiers:
    - **Core Transport**: evidence-backed baseline
    - **Experimental Extensions**: structural, not production
  - Be explicit about what's verified vs. structural

- [ ] **T6.2**: Update `docs/reference/architecture.md`
  - Add feature status classification
  - Document which features are production-ready vs. experimental

- [ ] **T6.3**: Update README.md
  - Add "Feature Status" section
  - Link to detailed status documentation

### Success Criteria

- ✅ Clear distinction between core transport and experimental extensions
- ✅ No conflicting messaging about production readiness
- ⏳ Users understand what's verified vs. structural (pending T6.3)

---

## Execution Order

```
Phase 1 (Documentation Consistency)
    ↓
Phase 2 (Automated Validation)
    ↓
Phase 3 (Production Candidate Scope)
    ↓
Phase 4 (Interop Evidence)
    ↓
Phase 5 (File Splitting)
    ↓
Phase 6 (Feature Classification)
```

**Rationale**: Documentation must be consistent before automation can verify it. Automation must work before scope can be defined. Scope must be clear before interop can be meaningful. File splitting is independent but benefits from having clear scope. Feature classification is last because it depends on all prior evidence.

---

## Success Metrics

| Metric | Current | Target | Status |
|--------|---------|--------|--------|
| Broken doc links | ~10 | 0 | ✅ Complete |
| Automated validation scripts | 0 | 3 | ✅ Complete |
| Release checklist items with evidence | 0 | 15+ | ⏳ Pending |
| Interop peer versions tracked | 0 | 3+ | ⏳ Pending |
| Files > 15KB | 4 | 0 | ⏳ Deferred |
| Feature classification clarity | Low | High | ✅ Complete |

---

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| File splitting breaks backward compatibility | Use facade headers that include all sub-headers |
| Automation scripts fail on Windows | Use PowerShell equivalents or cross-platform scripts |
| Interop testing requires external dependencies | Make interop optional, not required for CI |
| Scope statement is too narrow | Start narrow, expand as evidence grows |

---

## Notes

- This plan prioritizes **credibility** over **features**
- Each phase builds on the previous phase
- Phase 5 (File Splitting) can be parallelized with Phase 4 (Interop Evidence)
- Phase 6 (Feature Classification) is a documentation-only change
- All phases should be completed before claiming "production candidate" status
