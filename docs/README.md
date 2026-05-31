# FlowQ Documentation

FlowQ is a C++20 QUIC transport library under production hardening.

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

- **Status**: Non-production; production-candidate gate is tracked in [readiness-gate.md](production/readiness-gate.md).
- **Tests**: Use the CMake/CTest commands in [Testing](guides/testing.md) and the root README.

### Completed Features

- ✅ QUIC transport core (varint, frame, header codecs, packet pipeline, packet-number helpers, transport parameters)
- ✅ Connection management (connection loop, streams, flow control)
- ✅ Packet-protection boundaries (crypto provider, TLS adapter, key lifecycle, OpenSSL-gated AES-128-GCM packet protector)
- ✅ Recovery (ACK/loss, congestion control, recovery timers)
- ✅ Diagnostics (event sink, qlog-style observability)
- ✅ PATH_CHALLENGE/PATH_RESPONSE frame codec and Application-space response scheduling
- ✅ HTTP/3/QPACK (structural module)
- ✅ WebTransport (structural module)
- ✅ 0-RTT (experimental: early data support, replay protection)
- ✅ Congestion control (NewReno, BBR, CUBIC)

HTTP/3, QPACK, and 0-RTT headers are source/test modules only in the current production-candidate boundary and are excluded from the installed package.
