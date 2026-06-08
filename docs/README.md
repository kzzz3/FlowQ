# FlowQ Documentation

FlowQ is a C++23 QUIC transport library with production-grade security, multi-cipher support, and interop validation.

## Documentation Structure

```
docs/
├── README.md
├── guides/
│   ├── getting-started.md
│   ├── building.md
│   ├── testing.md
│   └── api-documentation.md
├── production/
│   ├── readiness-gate.md
│   └── release-checklist.md
├── reference/
│   ├── architecture.md
│   └── zero-copy-design.md
├── benchmarks/
│   ├── README.md
│   ├── performance.md
│   ├── soak.md
│   ├── loss-reordering.md
│   └── migration.md
└── interop/
    └── results.md
```

## References

- [Getting Started](guides/getting-started.md) - Local build and test workflow
- [Building](guides/building.md) - CMake presets, options, install, and package consumption
- [Testing](guides/testing.md) - Unit, integration, fuzz, and interop test commands
- [Architecture](reference/architecture.md) - Protocol, security, endpoint, and package boundaries
- [Production Readiness](production/readiness-gate.md) - Evidence and gate status
- [Release Checklist](production/release-checklist.md) - Current checklist

## Status

- **Version**: 1.1.0
- **Tests**: 518/518 passing
- **Installed API**: QUIC transport headers only. HTTP/3, QPACK, 0-RTT, and test-support interop headers are excluded from installation.
- **Default build**: Tests enabled; experimental examples excluded unless `FLOWQ_BUILD_EXPERIMENTAL_EXAMPLES=ON`.
- **Interop**: aioquic 1.3.0 + ngtcp2 1.20.0 verified (handshake, stream echo, loss recovery).
- **Congestion control**: CUBIC (default), NewReno, BBR with pacing controller.
- **Zero-copy**: Fully integrated via `enable_zero_copy` flag.
