# Building FlowQ

## Build Presets

FlowQ uses CMake presets for reproducible builds.

### Windows MSVC/vcpkg (Default)

```powershell
# Configure
$env:VCPKG_ROOT = "D:/vcpkg"
cmake --preset windows-msvc-vcpkg

# Build
cmake --build --preset windows-msvc-vcpkg

# Test
ctest --preset windows-msvc-vcpkg --timeout 10
```

### Linux GCC/vcpkg

```bash
export VCPKG_ROOT="$HOME/vcpkg"

cmake --preset linux-gcc-vcpkg
cmake --build --preset linux-gcc-vcpkg
ctest --preset linux-gcc-vcpkg --timeout 10 --output-on-failure
```

The complete Linux package gate is:

```bash
./scripts/validate-build.sh --preset linux-gcc-vcpkg
```

### Strict Warnings

```bash
cmake --preset linux-gcc-vcpkg-strict
cmake --build --preset linux-gcc-vcpkg-strict
ctest --preset linux-gcc-vcpkg-strict --timeout 10 --output-on-failure
```

On Windows, configure `windows-msvc-vcpkg` with `FLOWQ_ENABLE_STRICT_WARNINGS=ON` to enable `/W4 /WX /permissive- /EHsc` for project targets.

### Sanitizers

ASan/UBSan validation is a Linux gate:

```bash
export VCPKG_ROOT="$HOME/vcpkg"
./scripts/validate-sanitizers.sh
```

This uses the `linux-asan-ubsan` preset with `-fsanitize=address,undefined -fno-omit-frame-pointer`.

### OpenSSL QUIC TLS (Optional)

```powershell
# Configure with OpenSSL backend
cmake -S . -B build/openssl -G "Visual Studio 18 2026" `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_MANIFEST_FEATURES=openssl-quic-tls `
  -DFLOWQ_ENABLE_OPENSSL_QUIC_TLS=ON

# Build
cmake --build build/openssl --config Debug

# Test
ctest --test-dir build/openssl -C Debug --timeout 10
```

## Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `FLOWQ_BUILD_TESTS` | ON | Build test suite |
| `FLOWQ_BUILD_SOURCE_ONLY_EXAMPLES` | OFF | Build source-only examples outside the production-candidate API |
| `FLOWQ_BUILD_FUZZ` | OFF | Build fuzz targets (requires libFuzzer) |
| `FLOWQ_BUILD_INTEROP` | OFF | Build interop harness |
| `FLOWQ_ENABLE_STRICT_WARNINGS` | OFF | Treat project compiler warnings as errors |
| `FLOWQ_ENABLE_OPENSSL_QUIC_TLS` | OFF | Enable OpenSSL QUIC TLS backend |

## Install and Package

```powershell
# Install
cmake --install build/windows-msvc-vcpkg --config Debug --prefix build/install

# Build package consumer
cmake -S tests/package-consumer -B build/consumer `
  -G "Visual Studio 18 2026" `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_PREFIX_PATH="F:/Project/FlowQ/build/install"

# Run consumer
cmake --build build/consumer --config Debug
& "build/consumer/Debug/flowq_package_consumer.exe"
```

## Dependencies

Managed via vcpkg manifest (`vcpkg.json`):

- **asio**: Standalone Asio for async I/O
- **catch2**: Testing framework
- **stdexec**: Sender/receiver execution (future use)
- **openssl**: Optional, for crypto and TLS backends

## Troubleshooting

### vcpkg not found

Ensure `VCPKG_ROOT` environment variable points to your vcpkg installation.

### Build errors with OpenSSL

OpenSSL QUIC TLS requires OpenSSL 3.5+ with QUIC API support. Verify your vcpkg OpenSSL version.

### Test failures

Run with `--output-on-failure` to see detailed test output:
```powershell
ctest --preset windows-msvc-vcpkg --timeout 10 --output-on-failure
```
