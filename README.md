# FlowQ

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![CMake](https://img.shields.io/badge/build-CMake-informational)
![Tests](https://img.shields.io/badge/tests-514%20passing-green)
![QUIC](https://img.shields.io/badge/protocol-QUIC%20v1-informational)
![TLS](https://img.shields.io/badge/TLS-1.3-blue)

FlowQ is a C++20 QUIC transport library with production-grade security, multi-cipher support, and interop validation.

## Features

- **QUIC v1 transport**: varints, frames, packet headers, streams, ACK/loss, flow control, routing, timers
- **Multi-cipher AEAD**: AES-128-GCM, AES-256-GCM, ChaCha20-Poly1305 with RFC 9001 header protection
- **Congestion control**: CUBIC (default, RFC 8312 with TCP friendliness and fast convergence), NewReno, BBR with pacing controller
- **Key rotation**: RFC 9000 Section 6 key update support
- **Key erasure**: platform-specific key material zeroing (SecureZeroMemory/memset_s/explicit_bzero)
- **Interop validated**: aioquic 1.3.0 + ngtcp2 1.20.0

## Quick Start

### Requirements

- C++20 compiler (MSVC 2022, GCC 13+, Clang 16+)
- CMake 3.25+
- vcpkg with `VCPKG_ROOT` set

### Build & Test

**Windows:**
```powershell
$env:VCPKG_ROOT = "D:/vcpkg"
cmake --preset windows-msvc-vcpkg
cmake --build --preset windows-msvc-vcpkg
ctest --preset windows-msvc-vcpkg --timeout 60
```

**Linux:**
```bash
export VCPKG_ROOT="$HOME/vcpkg"
cmake --preset linux-gcc-vcpkg
cmake --build --preset linux-gcc-vcpkg
ctest --preset linux-gcc-vcpkg --timeout 60
```

## Documentation

- [Getting Started](docs/guides/getting-started.md) - First steps
- [Building](docs/guides/building.md) - Build options and presets
- [Testing](docs/guides/testing.md) - Running tests
- [Architecture](docs/reference/architecture.md) - System design
- [Production Readiness](docs/production/readiness-gate.md) - Release evidence

## Project Structure

```
FlowQ/
├── include/flowq/      # Header-only library
├── tests/              # Unit, integration, interop, fuzz tests
├── examples/           # Example applications
├── docs/               # Documentation
├── scripts/            # Build and validation scripts
└── CMakeLists.txt      # Build system
```

## Tech Stack

- **C++20** - Modern, value-oriented protocol code
- **CMake** - Build system with presets
- **vcpkg** - Dependency management
- **OpenSSL 3.5+** - Crypto and TLS backends
- **Catch2** - Testing framework
- **standalone Asio** - Async I/O

## Contributing

1. Use TDD: write failing test → implement → refactor
2. Keep changes atomic: implementation + tests together
3. Run full test suite before submitting: `ctest --preset <preset> --timeout 10`
4. No type suppressions (`as any`, `@ts-ignore`) or empty catch blocks

## License

See [LICENSE](LICENSE) for details.
