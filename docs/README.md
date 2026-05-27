# FlowQ Documentation

FlowQ is a modern C++20 QUIC-like protocol library.

## Documentation Structure

```
docs/
├── README.md              # This file
├── plan.md                # Project plan and milestones
├── guides/                # How-to guides
│   ├── getting-started.md
│   ├── building.md
│   ├── testing.md
│   └── contributing.md
├── reference/             # Technical reference
│   └── architecture.md
├── milestones/            # Milestone tracking
│   └── roadmap.md
├── production/            # Production readiness
│   ├── readiness-gate.md
│   └── release-checklist.md
└── archive/               # Historical documents
    ├── history.md
    └── ... (design specs)
```

## Quick Links

### For New Users
- [Getting Started](guides/getting-started.md) - First steps
- [Building](guides/building.md) - Build instructions
- [Testing](guides/testing.md) - Run and write tests

### For Contributors
- [Contributing](guides/contributing.md) - Development workflow
- [Architecture](reference/architecture.md) - System design
- [Roadmap](milestones/roadmap.md) - Milestone tracking

### For Release Managers
- [Plan](plan.md) - Project plan
- [Readiness Gate](production/readiness-gate.md) - Evidence requirements
- [Release Checklist](production/release-checklist.md) - Pre-release verification

## Project Status

**Version**: 0.1.0  
**Tests**: 318/318 passing  
**Status**: Non-production baseline

### Completed Features

- ✅ QUIC protocol primitives (varint, frame, header codecs)
- ✅ Connection management (connection loop, streams, flow control)
- ✅ Security boundaries (crypto provider, TLS adapter, key lifecycle)
- ✅ Recovery (ACK/loss, congestion control, recovery timers)
- ✅ Diagnostics (event sink, qlog-style observability)
- ✅ HTTP/3 (basic frame encoding)
- ✅ 0-RTT (early data support, replay protection)

### Milestones

All milestones M27-M39 are complete. See [Roadmap](milestones/roadmap.md) for details.

## Archive

Historical design documents are preserved in [archive/](archive/):
- [Development History](archive/history.md) - Consolidated development record
- [Technical Proposal](archive/technical-proposal.md) - Original project proposal
- [Basic Complete Declaration](archive/basic-complete.md) - Baseline declaration
- Design specs for completed milestones (M1-M20)
