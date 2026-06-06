# Testing FlowQ

## Test Framework

FlowQ uses [Catch2](https://github.com/catchorg/Catch2) for deterministic unit and integration tests.

## Running Tests

### Full Test Suite

```powershell
ctest --preset windows-msvc-vcpkg --timeout 60 --output-on-failure
```

### Specific Test Categories

```powershell
# Run only unit tests
ctest --preset windows-msvc-vcpkg -R "unit" --timeout 10

# Run only integration tests
ctest --preset windows-msvc-vcpkg -R "integration" --timeout 10

# Run specific test by name
& "build/windows-msvc-vcpkg/tests/Debug/flowq_unit_tests.exe" "test name pattern*"
```

### Test Verbosity

```powershell
# Show all test names
& "build/windows-msvc-vcpkg/tests/Debug/flowq_unit_tests.exe" -l

# Run with verbose output
& "build/windows-msvc-vcpkg/tests/Debug/flowq_unit_tests.exe" -s
```

## Test Structure

```
tests/
├── unit/                    # Unit tests (per-module)
│   ├── quic_connection_tests.cpp
│   ├── quic_session_tests.cpp
│   ├── quic_congestion_tests.cpp
│   └── ...
├── integration/             # Integration tests
│   ├── quic_loopback_tests.cpp
│   ├── quic_udp_session_tests.cpp
│   └── quic_endpoint_driver_tests.cpp
├── interop/                 # Interop tools (opt-in)
│   ├── test_interop.py
│   ├── test_aioquic_runner_script.py
│   ├── test_interop_evidence_validator.py
│   └── ngtcp2_initial_smoke.cpp
├── fuzz/                    # Fuzz targets
│   ├── fuzz_packet_header.cpp
│   ├── fuzz_frame_decode.cpp
│   └── fuzz_qpack.cpp
└── package-consumer/        # Package consumption test
    └── main.cpp
```

## Test Categories

### Unit Tests

Test individual modules in isolation:
- Protocol codecs (varint, frame, header)
- Stream state (receive, send, flow control)
- ACK/loss detection
- Congestion control
- Key lifecycle
- Diagnostics

### Integration Tests

Test module interactions:
- Connection loop behavior
- Session façade
- UDP session adapter
- Endpoint driver

### Interop Evidence Tests

The aioquic full-flow runner records JSON evidence under `docs/interop/results`. The release gate validates that evidence with:

```powershell
python scripts\validate-interop-evidence.py --results-dir docs\interop\results --min-full-flow-peers 1
```

The strict production-candidate gate uses `--min-full-flow-peers 2`; it remains blocked until a second external peer records the required full-flow scenarios.

CTest also covers the checked-in evidence validator and, on Windows, the aioquic PowerShell runner fail-closed behavior:

```powershell
ctest --preset windows-msvc-vcpkg-interop-openssl -R "flowq\.(interop_evidence_validator|aioquic_runner_script)" --output-on-failure
```

### Fuzz Tests

Opt-in fuzzing for robustness:
```powershell
# Build with fuzzing enabled
cmake -S . -B build-fuzz -G Ninja `
  -DCMAKE_CXX_COMPILER=clang++ `
  -DFLOWQ_BUILD_FUZZ=ON

# Run fuzz targets
./build-fuzz/flowq_packet_header_fuzz -max_total_time=60
```

## Writing Tests

### Test Naming Convention

```cpp
TEST_CASE("descriptive name of what is being tested") {
    // Arrange
    // Act
    // Assert
}
```

### Using Sections

```cpp
TEST_CASE("test with multiple scenarios") {
    SECTION("scenario A") {
        // Test scenario A
    }
    SECTION("scenario B") {
        // Test scenario B
    }
}
```

### Custom Matchers

```cpp
REQUIRE_THAT(result, Catch::Matchers::Predicate<...>([](auto& val) {
    return val.ok();
}));
```

## CI/CD

Tests run automatically on GitHub Actions:
- **ci.yml**: Windows MSVC/vcpkg build, test, install, package-consumer
- **robustness.yml**: Linux sanitizer/fuzz testing (Ubuntu)
- Release-readiness scripts validate documentation, checklist state, packet-protection boundaries, and checked-in interop evidence.
