# Contributing to FlowQ

## Development Workflow

### 1. Create a Feature Branch

```bash
git checkout -b feature/your-feature-name
```

### 2. Follow TDD

1. Write failing tests first (RED)
2. Implement minimal code to pass (GREEN)
3. Refactor while keeping tests green

### 3. Commit Messages

Use semantic commit style:

```
feat: add new feature
fix: resolve bug
docs: update documentation
refactor: improve code structure
test: add or update tests
chore: maintenance tasks
```

### 4. Verify Before Submitting

```powershell
# Build and test
cmake --build --preset windows-msvc-vcpkg
ctest --preset windows-msvc-vcpkg --timeout 10 --output-on-failure

# Install and package consumer
cmake --install build/windows-msvc-vcpkg --config Debug --prefix build/install
cmake -S tests/package-consumer -B build/consumer -G "Visual Studio 18 2026" `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_PREFIX_PATH="F:/Project/FlowQ/build/install"
cmake --build build/consumer --config Debug
```

### 5. Create Pull Request

- Describe the change
- Reference any related issues
- Ensure CI passes

## Code Style

### Naming Conventions

- **Types**: `snake_case` (e.g., `connection_loop`, `stream_send_state`)
- **Functions**: `snake_case` (e.g., `on_packet_sent`, `can_send`)
- **Constants**: `snake_case` or `UPPER_CASE` for macros
- **Enums**: `snake_case` values (e.g., `congestion_phase::slow_start`)

### Header Organization

```cpp
#pragma once

// Project headers
#include <flowq/buffer.hpp>
#include <flowq/error.hpp>

// Standard library headers
#include <cstdint>
#include <vector>

namespace flowq::quic {

// Implementation

} // namespace flowq::quic
```

### Error Handling

Use result types with `ok()` pattern:

```cpp
struct result_type {
    flowq::error error{};
    
    [[nodiscard]] bool ok() const noexcept {
        return error.ok();
    }
};
```

### Documentation

Add `///` comments to public APIs:

```cpp
/// Brief description of the function.
/// @param param1 Description of param1
/// @return Description of return value
[[nodiscard]] return_type function_name(param_type param1) noexcept;
```

## Architecture Principles

1. **Values First**: Pure value types before stateful components
2. **Deterministic Tests**: No network, no TLS, no random in unit tests
3. **Boundary Seams**: Virtual interfaces for external dependencies
4. **Fail-Closed**: Reject unsafe operations by default
5. **Non-Production Claims**: Never claim production readiness without evidence

## File Organization

```
include/flowq/quic/    # Public headers (header-only library)
tests/unit/            # Unit tests
tests/integration/     # Integration tests
docs/                  # Documentation
examples/              # Example applications
```

## Getting Help

- Check existing documentation in `docs/`
- Review test examples in `tests/`
- Open an issue for bugs or feature requests
