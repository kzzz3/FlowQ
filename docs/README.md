# FlowQ Documentation

FlowQ is a modern C++20 QUIC-like protocol library.

## Documentation Structure

```
docs/
├── README.md              # This file
├── plan.md                # Project plan
├── guides/                # How-to guides
│   ├── getting-started.md
│   ├── building.md
│   └── testing.md
├── milestones/            # Milestone tracking
│   └── roadmap.md
├── production/            # Production readiness
│   ├── readiness-gate.md
│   └── release-checklist.md
└── reference/             # Technical reference
    └── architecture.md
```

## Quick Links

- [Getting Started](guides/getting-started.md) - First steps
- [Building](guides/building.md) - Build instructions
- [Testing](guides/testing.md) - Run and write tests
- [Architecture](reference/architecture.md) - System design
- [Roadmap](milestones/roadmap.md) - Milestone tracking
- [Plan](plan.md) - Project plan

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

All milestones M27-M39 are complete. See [Roadmap](milestones/roadmap.md) for details.
