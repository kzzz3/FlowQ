# FlowQ Documentation

Welcome to the FlowQ documentation. FlowQ is a modern C++20 QUIC-like protocol library.

## Quick Links

- [Getting Started](guides/getting-started.md) - Get up and running quickly
- [Building](guides/building.md) - Build instructions and options
- [Testing](guides/testing.md) - Running and writing tests
- [Contributing](guides/contributing.md) - Development workflow and guidelines

## Documentation Structure

### 📚 Guides

Step-by-step instructions for common tasks:

- [Getting Started](guides/getting-started.md) - First steps with FlowQ
- [Building](guides/building.md) - Build configuration and options
- [Testing](guides/testing.md) - Test execution and writing
- [Contributing](guides/contributing.md) - Development workflow

### 📖 Reference

Technical reference documentation:

- [Architecture](reference/architecture.md) - System design and components
- [API Reference](reference/api.md) - Public API documentation (coming soon)

### 🎯 Milestones

Development milestone documentation:

- [Milestone Index](milestones/2026-04-27-post-basic-production-readiness-roadmap.md) - Complete roadmap
- [M01-M10: Core Primitives](milestones/M01-M10-core-primitives/) - Protocol foundations
- [M11-M20: Connection Integration](milestones/M11-M20-connection-integration/) - Connection management
- [M21-M27: Library Surface](milestones/M21-M27-library-surface/) - Public API
- [M28-M33: Crypto & Security](milestones/M28-M33-crypto-security/) - Security boundaries
- [M34-M36: Recovery & Routing](milestones/M34-M36-recovery-routing/) - Recovery and routing
- [M37-M39: Diagnostics & Interop](milestones/M37-M39-diagnostics-interop/) - Observability

### 🔒 Production

Production readiness documentation:

- [Readiness Gate](production/readiness-gate.md) - Evidence requirements
- [Release Checklist](production/release-checklist.md) - Pre-release verification

### 📦 Archive

Historical design documents and completed plans:

- [Design Specs](archive/) - Milestone design specifications
- [Technical Proposal](archive/technical-proposal.md) - Original project proposal
- [Basic Complete](archive/basic-complete.md) - Baseline declaration

## Project Status

**Current Version**: 0.1.0

**Status**: Non-production baseline with 318 tests passing

**Completed Milestones**: M27-M39 (production-readiness track)

**Key Features**:
- ✅ QUIC protocol primitives (varint, frame, header codecs)
- ✅ Connection management (connection loop, streams, flow control)
- ✅ Security boundaries (crypto provider, TLS adapter, key lifecycle)
- ✅ Recovery (ACK/loss, congestion control, recovery timers)
- ✅ Diagnostics (event sink, qlog-style observability)
- ✅ HTTP/3 (basic frame encoding)
- ✅ 0-RTT (early data support, replay protection)

## Finding Information

### By Topic

- **Protocol**: [Architecture](reference/architecture.md), [M01-M10 milestones](milestones/M01-M10-core-primitives/)
- **Connection**: [M11-M20 milestones](milestones/M11-M20-connection-integration/)
- **Security**: [M28-M33 milestones](milestones/M28-M33-crypto-security/)
- **Testing**: [Testing Guide](guides/testing.md)
- **Building**: [Building Guide](guides/building.md)

### By Role

- **New Developer**: Start with [Getting Started](guides/getting-started.md)
- **Contributor**: Read [Contributing](guides/contributing.md) and [Architecture](reference/architecture.md)
- **User**: Check [API Reference](reference/api.md) (coming soon)
- **Release Manager**: Review [Release Checklist](production/release-checklist.md)

## Contributing to Documentation

Documentation improvements are welcome! See [Contributing](guides/contributing.md) for guidelines.

### Documentation Standards

- Use Markdown format
- Include code examples where helpful
- Keep documentation up-to-date with code changes
- Use relative links within the docs folder
