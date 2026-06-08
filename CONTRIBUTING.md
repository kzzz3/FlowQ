# Contributing to FlowQ

## Development Workflow

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-change`
3. Write a failing test
4. Implement the feature
5. Run the full test suite
6. Submit a pull request

## Code Style

- C++23 standard
- `snake_case` for functions, variables, types
- `PascalCase` for classes, enums
- `UPPER_SNAKE_CASE` for macros
- All public APIs must have Doxygen comments
- No type suppressions (`as any`, `@ts-ignore`)
- No empty catch blocks
- No TODO/FIXME in production code

## Testing

```powershell
# Windows
ctest --preset windows-msvc-vcpkg --timeout 60

# Linux
ctest --preset linux-gcc-vcpkg --timeout 60
```

All tests must pass before submitting a PR.

## Pull Request Guidelines

- Keep changes atomic: implementation + tests together
- One logical change per PR
- Reference related issues
- Update documentation if API changes

## Build Requirements

- C++23 compiler (MSVC 2022, GCC 13+, Clang 16+)
- CMake 3.25+
- vcpkg with `VCPKG_ROOT` set
