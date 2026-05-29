# Getting Started with FlowQ

FlowQ is a C++20 QUIC transport library under production hardening. This guide covers the local build and test workflow.

## Prerequisites

- **Windows**: Visual Studio 18 2026 or later
- **CMake**: 3.25 or newer
- **vcpkg**: Set `VCPKG_ROOT` environment variable

## Quick Start

### 1. Clone the Repository

```bash
git clone <repository-url>
cd FlowQ
```

### 2. Configure with vcpkg

```powershell
$env:VCPKG_ROOT = "D:/vcpkg"
cmake --preset windows-msvc-vcpkg
```

### 3. Build

```powershell
cmake --build --preset windows-msvc-vcpkg
```

### 4. Run Tests

```powershell
ctest --preset windows-msvc-vcpkg --timeout 10
```

## What's Included

- **Core Protocol**: QUIC varint, packet-number, frame, header, packet pipeline, and transport-parameter codecs
- **Connection Management**: Connection loop, stream state, flow control
- **Packet Protection**: Crypto provider boundaries, TLS handshake adapter, key lifecycle, and OpenSSL-gated AES-128-GCM packet protection
- **Recovery**: ACK/loss detection, congestion control, recovery timers
- **Diagnostics**: Event sink, qlog-style observability
- **Path Validation Primitives**: PATH_CHALLENGE/PATH_RESPONSE codec and Application-space response scheduling
- **Structural Extensions**: HTTP/3/QPACK, WebTransport, and 0-RTT modules outside the production-candidate scope

## Project Structure

```
FlowQ/
├── include/flowq/quic/    # Public header-only library
├── tests/unit/            # Unit tests (Catch2)
├── tests/integration/     # Integration tests
├── tests/interop/         # Interop harness (opt-in)
├── tests/fuzz/            # Fuzz targets
├── examples/              # Example applications
└── docs/                  # Documentation
```

## Next Steps

- Read [Building](building.md) for detailed build instructions
- Read [Testing](testing.md) for test execution guide
- Read [Architecture](../reference/architecture.md) for design overview
