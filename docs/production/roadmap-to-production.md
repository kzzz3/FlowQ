# FlowQ Production Roadmap

The authoritative gate status is maintained in [readiness-gate.md](readiness-gate.md) and [release-checklist.md](release-checklist.md). This roadmap tracks only current work, not historical releases.

## Current Status

| Area | Status |
|------|--------|
| Version | 1.0.0 evidence set |
| Windows tests | 528/528 passing |
| Linux tests | 510/511 passing |
| ASan/UBSan | 0 errors |
| Full-flow interop | aioquic 1.3.0 |
| Interop evidence gate | 1+ full-flow peer in normal gate, 2+ in strict gate |
| Structural interop smoke | vcpkg-backed optional ngtcp2 Initial packet generation target |
| Cipher suites | AES-128/256-GCM, ChaCha20-Poly1305 |
| Congestion control | NewReno, BBR, CUBIC + pacing |

## Active Production Gates

1. Record a second external peer with full handshake and stream scenario evidence so the strict interop evidence validator passes with `--min-full-flow-peers 2`.
2. Record a human security review in `docs/security/reviews/human-security-review.md` so `scripts/validate-security-evidence.py` can verify the strict-gate fields, `YYYY-MM-DD` date, and commit SHA.
3. Record an external security audit in `docs/security/audits/external-security-audit.md` before public secure or production-ready claims, with the same strict-gate field formats.
4. Keep package installation limited to the production QUIC transport API.
5. Keep documentation synchronized with the current source tree and evidence only.
