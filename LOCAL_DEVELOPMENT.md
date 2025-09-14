# Local Development with Real WASM

This guide explains how to use the `test_with_real_wasm.sh` script for comprehensive local development and testing with real WebAssembly artifacts.

## Quick Start

```bash
# Full build and test (most common usage)
./test_with_real_wasm.sh

# With Perfetto tracing support
ENABLE_PERFETTO=1 ./test_with_real_wasm.sh

# Quick iteration (skip WASM rebuild)
SKIP_WASM_BUILD=1 ./test_with_real_wasm.sh
```

## What the Script Does

The script automates the complete development workflow:

1. **Dependency Checks**: Verifies all required tools and dependencies
2. **Perfetto Dependencies**: Builds protobuf and abseil libraries if needed
3. **WASM Build**: Runs `./build.sh` to create WebAssembly modules
4. **Artifact Copying**: Copies WASM files to `packages/core/wasm/`
5. **Core Tests**: Runs TypeScript tests in `packages/core/`
6. **Integration Tests**: Runs WASM integration tests in `musashi-wasm-test/`

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `ENABLE_PERFETTO` | `0` | Enable Perfetto tracing support in WASM build |
| `SKIP_WASM_BUILD` | `0` | Skip WASM build step (use existing artifacts) |
| `SKIP_CORE_TESTS` | `0` | Skip TypeScript tests in packages/core |
| `SKIP_INTEGRATION` | `0` | Skip integration tests in musashi-wasm-test |
| `VERBOSE` | `0` | Show detailed output for debugging |

## Common Usage Patterns

### Full Development Cycle
```bash
# Complete build and test with all features
ENABLE_PERFETTO=1 ./test_with_real_wasm.sh
```

### Fast Iteration
```bash
# Quick test after code changes (skip WASM rebuild)
SKIP_WASM_BUILD=1 ./test_with_real_wasm.sh
```

### Debugging Build Issues
```bash
# Verbose output for troubleshooting
VERBOSE=1 ./test_with_real_wasm.sh
```

### Testing Specific Components
```bash
# Only test integration (skip TypeScript tests)
SKIP_WASM_BUILD=1 SKIP_CORE_TESTS=1 ./test_with_real_wasm.sh

# Only test core package (skip integration)
SKIP_WASM_BUILD=1 SKIP_INTEGRATION=1 ./test_with_real_wasm.sh
```

## Prerequisites

### Required Tools
- **Emscripten SDK (EMSDK)**: WebAssembly compiler toolchain
- **Node.js 16+**: JavaScript runtime
- **bash**: For build.sh script execution
- **npm**: Package manager

### EMSDK Setup
```bash
# Download and install EMSDK
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh

# Verify installation
emcc --version
```

### Perfetto Prerequisites
For Perfetto tracing support, the script automatically handles:
- Protobuf library compilation
- Abseil library compilation  
- Protobuf file generation

These are built automatically when `ENABLE_PERFETTO=1` is used.

## Output and Logging

### Success Output
```
[SUCCESS] All tests completed successfully!
[INFO] Total elapsed time: 45s
✅ Ready for development!
```

### Error Handling
The script provides detailed error messages and will:
- Show dependency check failures
- Display build errors with log file references
- Report test failures with relevant output

### Verbose Mode
Enable with `VERBOSE=1` to see:
- All command execution
- Real-time build output
- Detailed progress information

## Troubleshooting

### Common Issues

**EMSDK not found**
```bash
# Solution: Install and activate EMSDK
source /path/to/emsdk/emsdk_env.sh
```

**Emscripten not in PATH**
```bash
# Solution: Install/activate EMSDK and ensure emcc is in PATH
source /path/to/emsdk/emsdk_env.sh
```

**Perfetto build fails**
```bash
# Solution: Build dependencies manually
./build_protobuf_wasm.sh
```

**WASM artifacts missing**
```bash
# Solution: Ensure build.sh completed successfully
./build.sh
ls -la musashi*.out.*
```

### Debug Information

The script creates temporary log files when not in verbose mode:
- `build_output.log` - WASM build output
- `core_test_output.log` - TypeScript test output  
- `integration_test_output.log` - Integration test output

These are automatically cleaned up on success but retained on failure for debugging.

## Integration with Development Workflow

### VS Code Integration
Add to `.vscode/tasks.json`:
```json
{
  "label": "Test with Real WASM",
  "type": "shell",
  "command": "./test_with_real_wasm.sh",
  "group": "test",
  "presentation": {
    "echo": true,
    "reveal": "always"
  }
}
```

### Pre-commit Hook
Add to `.git/hooks/pre-commit`:
```bash
#!/bin/bash
# Run quick validation before commit
SKIP_WASM_BUILD=1 ./test_with_real_wasm.sh
```

### CI/CD Validation
Test locally before pushing:
```bash
# Mimic CI environment
ENABLE_PERFETTO=1 ./test_with_real_wasm.sh
```

## Script Architecture

The script is designed with:
- **Fail-fast principle**: Stops on first error
- **Modular design**: Each step is independent
- **Robust error handling**: Clear error messages and cleanup
- **Flexible execution**: Skip steps via environment variables
- **Progress reporting**: Clear status updates throughout

## File Structure After Execution

```
musashi/
├── test_with_real_wasm.sh          # The script
├── musashi*.out.*                  # Built WASM artifacts (root)
├── packages/core/wasm/             # Copied WASM artifacts
│   ├── musashi-node.out.mjs
│   ├── musashi-node.out.wasm
│   └── ...
└── musashi-wasm-test/              # Integration test artifacts
    ├── musashi-node.out.mjs
    ├── musashi-node.out.wasm
    └── ...
```

This ensures all packages have access to the latest WASM builds for testing.
