# M20 Basic Complete Library Scope Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Freeze FlowQ's basic complete non-production QUIC library scope and synchronize the root roadmap/docs before M21 code work begins.

**Architecture:** This is a docs-only milestone. It creates the M20 scope spec, updates roadmap/status documents, marks the post-M10 roadmap as historical through M19, and verifies that production/security language remains bounded by M19's adapter seam.

**Tech Stack:** Markdown docs, C++20 project roadmap, CMake/vcpkg verification commands where relevant.

---

## File Structure

- Create `docs/superpowers/specs/2026-04-27-m20-basic-complete-library-scope-design.md`: authoritative M20 scope-freeze spec.
- Modify `docs/superpowers/plans/2026-04-27-post-m19-basic-quic-library-completion.md`: keep M20-M26 completion roadmap aligned with M20 scope.
- Modify `docs/superpowers/specs/2026-04-27-post-m10-roadmap.md`: mark it historical through M19 and fix stale UDP deferral wording.
- Modify `README.md`: distinguish M18 basic usable, M19 security boundary, and M20 basic-complete scope freeze.
- Modify `PLAN.md`: mark M20 complete after verification while keeping M21-M26 unchecked, and identify this milestone as docs-only scope freeze.
- Modify `docs/development.md`: add the M20 scope-freeze section after M19.

## Task 1: Scope Spec and Historical Roadmap Cleanup

**Files:**
- Create: `docs/superpowers/specs/2026-04-27-m20-basic-complete-library-scope-design.md`
- Modify: `docs/superpowers/specs/2026-04-27-post-m10-roadmap.md`
- Modify: `docs/superpowers/plans/2026-04-27-post-m19-basic-quic-library-completion.md`

- [x] **Step 1: Write the M20 scope spec**

  Include these acceptance bullets:

  ```markdown
  - FlowQ is described as a non-production QUIC-like C++ library baseline, not a production QUIC stack.
  - Basic complete requires public session API, UDP adapter smoke path, examples, packaging, CI, and docs.
  - Basic complete excludes real TLS, AEAD, header protection, congestion control, HTTP/3, and interoperability claims.
  - Future production tracks are listed as backlog, not implied current work.
  ```

- [x] **Step 2: Clarify post-M10 roadmap history**

  Update `docs/superpowers/specs/2026-04-27-post-m10-roadmap.md` so it says production/interoperable UDP public APIs remain deferred, while a bounded non-production UDP/ASIO smoke adapter is planned later under the post-M19 basic library track.

- [x] **Step 3: Keep post-M19 completion plan aligned**

  Ensure `docs/superpowers/plans/2026-04-27-post-m19-basic-quic-library-completion.md` lists M20 as scope freeze only and M21-M26 as implementation/productization milestones.

## Task 2: Root Documentation Synchronization

**Files:**
- Modify: `README.md`
- Modify: `PLAN.md`
- Modify: `docs/development.md`

- [x] **Step 1: Update README current status**

  Add language that M20 starts the scope-freeze stage for a basic complete non-production QUIC library baseline, and point to the post-M19 completion plan.

- [x] **Step 2: Update PLAN roadmap**

  Mark M20 complete after this docs/spec freeze is verified. Keep M21-M26 unchecked. Clarify that M21-M26 implement the public API, UDP adapter, scheduler, examples, packaging, and CI.

- [x] **Step 3: Add development docs section**

  Add `## QUIC basic-complete library scope freeze` after the M19 section in `docs/development.md`. Include the M18/M19/M20 distinction and the M20-M26 list.

## Task 3: Verification

**Files:**
- Inspect: `README.md`
- Inspect: `PLAN.md`
- Inspect: `docs/development.md`
- Inspect: `docs/superpowers/specs/2026-04-27-m20-basic-complete-library-scope-design.md`
- Inspect: `docs/superpowers/plans/2026-04-27-m20-basic-complete-library-scope.md`

- [x] **Step 1: Search for contradictory security/scope language**

  Run:

  ```powershell
  git -c safe.directory=F:/Project/FlowQ grep -n "production\|TLS\|AEAD\|header protection\|interoper" -- README.md PLAN.md docs/development.md docs/superpowers/specs docs/superpowers/plans
  ```

  Expected: every current-scope mention is paired with “non-production,” “test-only,” “deferred,” or “future adapter” context.

- [x] **Step 2: Scan for placeholders**

  Run:

  ```powershell
  Select-String -Path "docs/superpowers/specs/2026-04-27-m20-basic-complete-library-scope-design.md","docs/superpowers/plans/2026-04-27-m20-basic-complete-library-scope.md" -Pattern "TB[D]|TO[D]O|implemen[t] later|fill in detail[s]|appropriat[e]|similar t[o]"
  ```

  Expected: no matches.

- [x] **Step 3: Check formatting**

  Run:

  ```powershell
  git -c safe.directory=F:/Project/FlowQ diff --check
  ```

  Expected: no whitespace errors other than Windows LF-to-CRLF warnings.

- [x] **Step 4: Report working tree state**

  Run:

  ```powershell
  git -c safe.directory=F:/Project/FlowQ status --short
  ```

  Expected: only docs/roadmap files are modified or added.

## Self-Review

- Spec coverage: Covers basic-complete scope, M18/M19/M20 distinction, M21-M26 boundaries, docs sync, and production QUIC non-goals.
- Placeholder scan: No incomplete placeholders; the plan contains exact files, commands, and expected results.
- Type consistency: Uses existing milestone names and planned public header names from the post-M19 completion plan.
