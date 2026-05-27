# M39 Production Evidence Gate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Define the evidence gate required before FlowQ status can move from non-production baseline to production candidate.

**Architecture:** Status wording is evidence-bound. The gate records build/test/package, RFC vectors, backend versions, robustness gates, interop scenarios, Oracle review, and human security review requirements.

**Tech Stack:** Markdown release docs, existing CI gates, Oracle review, human security review outside the agent loop.

---

## Files

- Create: `docs/production-readiness-gate.md`
- Create: `docs/release-checklist.md`
- Modify: `README.md`
- Modify: `PLAN.md`
- Modify: `docs/basic-complete.md`
- Modify: `docs/development.md`

## Tasks

- [ ] Write release checklist requiring full CTest, install/package-consumer, selected RFC vector tests, sanitizer/fuzz gates, interop scenarios, external backend version record, Oracle review, and human security review.
- [ ] Define allowed statuses: `non-production baseline`, `production-readiness milestone`, `production candidate`, and `production-ready`.
- [ ] State that `production-ready` requires all release checklist gates plus human security review outside the agent loop.
- [ ] Require production-candidate scope text to list client/server support, QUIC version, operating systems, TLS backend name/version, cipher suites, interop peer versions, passed scenarios, and unsupported features such as migration, stateless reset, path validation, 0-RTT, HTTP/3, or WebTransport if still deferred.
- [ ] Add forbidden-claim scan guidance for docs before checklist completion.
- [ ] Run full local verification and changed-file scans.
- [ ] Request Oracle review for status wording.

## Acceptance Gate

FlowQ has a clear production candidate gate. Completing M39 alone does not make FlowQ production-ready; it defines how that claim can later be earned and how narrowly supported scope must be stated.
