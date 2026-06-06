# FlowQ Production Roadmap

The authoritative gate status is maintained in [readiness-gate.md](readiness-gate.md) and [release-checklist.md](release-checklist.md). This roadmap tracks only current work, not historical releases.

## Current Status

| Area | Status |
|------|--------|
| Version | 1.0.0 evidence set |
| Windows tests | 523/523 passing |
| Linux tests | 510/511 passing |
| ASan/UBSan | 0 errors |
| Full-flow interop | aioquic 1.3.0 |
| Structural interop smoke | Optional ngtcp2 Initial packet generation target |
| Cipher suites | AES-128/256-GCM, ChaCha20-Poly1305 |
| Congestion control | NewReno, BBR, CUBIC + pacing |

## Active Production Gates

1. Record a second external peer with full handshake and stream scenario evidence.
2. Record a human security review.
3. Record an external security audit before public secure or production-ready claims.
4. Keep package installation limited to the production QUIC transport API.
5. Keep documentation synchronized with the current source tree and evidence only.
