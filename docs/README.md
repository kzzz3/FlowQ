# FlowQ Documentation

FlowQ is a C++20 QUIC transport library under production hardening. These documents describe the current source tree, build gates, production boundary, and verification evidence.

## Documentation Structure

```
docs/
├── README.md
├── plan.md
├── guides/
│   ├── getting-started.md
│   ├── building.md
│   └── testing.md
├── milestones/
│   └── roadmap.md
├── production/
│   ├── readiness-gate.md
│   └── release-checklist.md
└── reference/
    └── architecture.md
```

## Current References

- [Getting Started](guides/getting-started.md) - Local build and test workflow
- [Building](guides/building.md) - CMake presets, options, install, and package consumption
- [Testing](guides/testing.md) - Unit, integration, fuzz, and interop test commands
- [Architecture](reference/architecture.md) - Current protocol, security, endpoint, and package boundaries
- [Production Readiness Gate](production/readiness-gate.md) - Evidence required for production-candidate wording
- [Release Checklist](production/release-checklist.md) - Current gate status
- [Current Scope](plan.md) - Current implementation scope and remaining production gates
- [Evidence Index](milestones/roadmap.md) - Current milestone-to-evidence mapping

## Current Status

- **Status**: Non-production; production-candidate wording remains blocked until all release gates and human review are recorded.
- **Installed API**: Production-scope QUIC transport headers only. HTTP/3, QPACK, 0-RTT, and test-support interop headers are excluded from installation.
- **Default build**: Tests are enabled; source-only examples outside the production-candidate API require `FLOWQ_BUILD_SOURCE_ONLY_EXAMPLES=ON`.
- **Interop evidence**: aioquic handshake, bidirectional stream echo, and loss-recovery scenarios are recorded in the production gate.
