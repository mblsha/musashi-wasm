# Contributing to musashi-wasm

Thank you for your interest in contributing to the Musashi M68k WebAssembly emulator! This document provides guidelines and information for contributors.

## Code of Conduct

Please be respectful and constructive in all interactions. We aim to maintain a welcoming and inclusive environment for all contributors.

## Getting Started

1. Fork the repository
2. Clone your fork locally
3. Create a feature branch from `master`
4. Make your changes
5. Run tests to ensure everything works
6. Submit a pull request

## Development Setup

### Prerequisites

- Node.js (v18 or later)
- Emscripten SDK (for WASM builds)
- CMake (for native builds)
- Fish shell (optional, for build scripts)

### Building

```bash
# Build WebAssembly modules
make wasm

# Build with Perfetto tracing
ENABLE_PERFETTO=1 ./build.fish

# Run tests
npm test
./test_with_real_wasm.sh
```

## Branch Protection Rules

The `master` branch is protected with the following requirements:

### Required Status Checks

All pull requests must pass these CI checks before merging:

- **build-all-targets** - Verifies all build targets compile
- **sanitizer-tests** - Memory safety checks (ASAN/UBSAN)
- **perfetto-build** - Perfetto integration build
- **wasm-build** - WebAssembly build verification
- **wasm-perfetto-build** - WebAssembly with Perfetto support

### Pull Request Requirements

- **At least 1 approval** from a code owner
- **All conversations resolved** before merging
- **Up-to-date with master** - branches must be rebased/merged with latest master
- **No force pushes** to master
- **No branch deletion** of master

### Code Ownership

The [CODEOWNERS](.github/CODEOWNERS) file defines automatic review assignments for different parts of the codebase. Changes to critical components like the CPU emulation core will automatically request review from the appropriate maintainers.

## Contribution Guidelines

### Code Style

- Follow existing code conventions in the file you're modifying
- Use existing libraries and utilities rather than adding new dependencies
- For C++ code: follow the existing style (spaces, not tabs)
- For TypeScript: use the provided ESLint/Prettier configuration

### Testing

- Add tests for new functionality
- Ensure all existing tests pass
- Run sanitizer builds locally if possible: `cmake -DENABLE_SANITIZERS=ON`
- Test both native and WASM builds

### Commit Messages

- Use clear, descriptive commit messages
- Reference issue numbers when applicable (#123)
- Follow conventional commit format when possible:
  - `feat:` for new features
  - `fix:` for bug fixes
  - `docs:` for documentation changes
  - `test:` for test additions/changes
  - `refactor:` for code refactoring

### Pull Request Process

1. **Create a draft PR early** for visibility
2. **Write a clear description** of your changes
3. **Include test results** or screenshots if applicable
4. **Link related issues** using GitHub keywords (Fixes #123)
5. **Be responsive to feedback** during code review
6. **Keep PRs focused** - one feature/fix per PR when possible

## Testing Your Changes

### Local Testing

```bash
# Run native tests
mkdir -p build && cd build
cmake .. -DBUILD_TESTS=ON
make -j8
ctest --output-on-failure

# Run WASM tests
cd musashi-wasm-test
npm test

# Run TypeScript tests
npm test
```

### CI Testing

All PRs trigger automatic CI runs. You can see the status of these checks on your PR page. If a check fails:

1. Click on the failing check for details
2. Review the error logs
3. Fix the issue locally
4. Push your fix to update the PR

## Areas for Contribution

### Good First Issues

Look for issues labeled `good first issue` for beginner-friendly tasks.

### Current Priorities

- Performance optimizations
- Additional M68k instruction support
- Documentation improvements
- Test coverage expansion
- TypeScript API enhancements

### Feature Requests

Please open an issue to discuss new features before implementing them. This helps ensure:
- The feature aligns with project goals
- We can provide guidance on implementation
- We avoid duplicate work

## Documentation

- Update relevant documentation when changing functionality
- Add JSDoc comments for new TypeScript APIs
- Update CLAUDE.md if changing build processes or architecture
- Keep README.md examples working and up-to-date

## Security

If you discover a security vulnerability, please:
1. **Do not** open a public issue
2. Email the maintainers directly
3. Allow time for a fix before public disclosure

## Questions?

- Open an issue for questions about contributing
- Check existing issues and PRs for similar discussions
- Review the [documentation](README.md) and [CLAUDE.md](CLAUDE.md)

## Recognition

Contributors are recognized in:
- Git history
- Release notes for significant contributions
- The AUTHORS file (for substantial contributions)

Thank you for helping improve musashi-wasm!