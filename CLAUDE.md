# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview
Musashi is a Motorola M68000 CPU emulator (v4.10) modified to compile to WebAssembly. The project emulates various M68k variants (68000, 68010, 68EC020, 68020, 68EC030, 68030, 68EC040, 68040) and provides a JavaScript API for web and Node.js environments.

## Build Commands

### Standard Build
```bash
# Build C object files and generate M68k instruction tables
make

# Build WebAssembly modules for both web and Node.js (ESM-only)
./build.fish  # Recommended - handles Emscripten detection automatically

# Alternative: use make wasm (less robust Emscripten detection)
make wasm
```

### Enhanced Build Script
The `build.fish` script provides robust Emscripten detection and configuration:
- **Automatic EMSDK Detection**: Supports both standard EMSDK installations and Homebrew
- **PATH Management**: Handles Fish shell PATH inheritance issues
- **Toolchain Verification**: Validates emcc, em++, emmake availability before building
- **Perfetto Support**: Enable with `ENABLE_PERFETTO=1 ./build.fish`
- **ESM Output**: Generates `.mjs` files for modern JavaScript module systems

### Build with Perfetto Tracing
```bash
# First, build protobuf dependencies (one-time setup)
./build_protobuf_wasm.sh

# Build with Perfetto tracing enabled
ENABLE_PERFETTO=1 ./build.fish
```

### Clean Build
```bash
make clean
./build.fish  # Rebuilds WASM modules with enhanced detection
```

## Architecture

### Code Generation Flow
1. `m68kmake.c` reads `m68k_in.c` (instruction definitions) and generates:
   - `m68kops.c` - Implementation of all M68k instructions
   - `m68kops.h` - Headers for instruction handlers
2. Core emulation in `m68kcpu.c` uses the generated instruction handlers
3. `myfunc.cc` provides WebAssembly-specific API and memory management

### Key Components
- **CPU Core**: `m68kcpu.c/h` - Main emulation logic, register management, execution loop
- **Instruction Generator**: `m68kmake.c` + `m68k_in.c` - Generates opcode implementations
- **Disassembler**: `m68kdasm.c` - Converts machine code to assembly
- **FPU**: `m68kfpu.c` - Floating-point unit emulation using softfloat library
- **WASM Interface**: `myfunc.cc` - Memory regions, callbacks, JavaScript interop

### WebAssembly Exports
Key functions exposed to JavaScript:
- CPU Control: `_m68k_init`, `_m68k_execute`, `_m68k_pulse_reset`, `_m68k_cycles_run`, `_m68k_end_timeslice`
- Register Access: `_m68k_get_reg`, `_m68k_set_reg`, `_get_d_reg`, `_set_d_reg`, `_get_a_reg`, `_set_a_reg`, `_get_pc_reg`, `_set_pc_reg`, `_get_sr_reg`, `_set_sr_reg`, `_get_sp_reg`
- Memory Callbacks: `_set_read_mem_func`, `_set_write_mem_func`, `_set_read8_callback`, `_set_write8_callback`, `_set_probe_callback`
- PC Hooks: `_set_pc_hook_func`, `_add_pc_hook_addr`, `_clear_pc_hook_addrs`, `_clear_pc_hook_func`
- Instruction Hooks: `_set_full_instr_hook_func`, `_clear_instr_hook_func` (gets PC, instruction register, and cycles)
- Memory Management: `_add_region`, `_clear_regions` (maps memory regions)
- Symbol Registration: `_register_function_name`, `_register_memory_name`, `_register_memory_range`, `_clear_registered_names`
- Debug: `_enable_printf_logging`, `_reset_myfunc_state`
- Tracing: `_m68k_trace_enable`, `_perfetto_init`, `_perfetto_export_trace` (when built with Perfetto)

### Configuration
- `m68kconf.h` - CPU feature configuration (currently set for basic M68000)
- `M68K_COMPILE_FOR_MAME` is set to 0 (not MAME-specific)
- Instruction hooks enabled via `M68K_INSTRUCTION_HOOK`

## Development Notes

### Modifying Instructions
To modify M68k instruction behavior:
1. Edit `m68k_in.c` for instruction definitions
2. Run `make` to regenerate `m68kops.c/h`
3. Rebuild WASM with `make wasm`

### Memory Access Pattern
The emulator uses callback functions for memory access:
- Read callbacks: `read_imm_8`, `read_imm_16`, `read_imm_32`, etc.
- Write callbacks: `write_imm_8`, `write_imm_16`, `write_imm_32`, etc.
- Memory regions are registered via `_add_region` with base address and size

### Emscripten Build Configuration
The WebAssembly build creates two versions:
- `musashi.out.*` - Web browser version with MODULARIZE
- `musashi-node.out.*` - Node.js version with different export settings

Both use optimization level -O2 and export all functions prefixed with underscore.

**Critical Emscripten Insights**:
- **Enhanced Detection**: `build.fish` automatically detects EMSDK or emcc in PATH
- **Multi-Platform Support**: Works with standard EMSDK and Homebrew installations
- **Fish Shell Compatibility**: Handles Fish shell PATH inheritance issues automatically
- **Toolchain Validation**: Verifies emcc/emmake availability before building
- **ESM-First**: Generates `.mjs` files for modern JavaScript environments
- **Symbol Export**: Extensive function export list includes new hook and utility functions
- **Dual Output**: Creates both Node.js and web-compatible WASM modules

## Running Native Tests

### Build and Run Tests
```bash
# Clean build with tests
mkdir -p build && cd build
cmake .. -DBUILD_TESTS=ON
make -j8

# Run all tests
ctest --output-on-failure

# Run specific test executable
./test_m68k
./test_myfunc
```

### Running WebAssembly Tests
```bash
# In musashi-wasm-test directory
npm test                          # Run all tests
npm test perfetto_wasm.test.js   # Run specific test
npm test -- --verbose            # Verbose output

# Test files must be in tests/ directory
# Tests use Jest framework with Node.js
```

### Debugging Test Failures
The M68k CPU tests require proper initialization:
1. **Reset Vector Setup**: The CPU reads initial SP from address 0 and PC from address 4
2. **Memory Callbacks**: Must be set before CPU operations (read_mem, write_mem, pc_hook)
3. **Instruction Execution**: Use m68k_execute(cycles) with sufficient cycles (>10 for most instructions)

### Testing Insights
- **Hook Verification**: PC hooks are the most reliable way to verify instruction execution - track them in a vector/array to verify execution flow
- **First Execution Overhead**: The first `m68k_execute()` after reset has ~40 cycle initialization overhead - tests should run a dummy execution in SetUp()
- **Memory Callbacks Required**: CPU execution requires memory read/write callbacks to be set before calling `m68k_execute()`
- **State Management**: Use `reset_myfunc_state()` to clear all hooks and callbacks between tests
- **Memory Management**: Use `clear_regions()` between tests to prevent memory leaks when using memory regions
- **Test Memory**: Tests use a 1MB memory buffer, ensure addresses stay within bounds
- **Sanitizers**: Use `-DENABLE_SANITIZERS=ON` for memory debugging on Linux/macOS
- **Hook System Testing**: Tests now use real WASM modules instead of mocks for more accurate validation

### Test Structure
Tests use Google Test framework with fixtures:
- `M68kTest`: Tests CPU core functionality
- `MyFuncTest`: Tests the myfunc.cc API wrapper

Each test fixture sets up:
- Memory buffer (1MB)
- Memory access callbacks
- Reset vector at addresses 0-7 (SP at 0, PC at 4)
- CPU initialization with m68k_init() and m68k_pulse_reset()
- Dummy execution with m68k_execute(1) to handle initialization overhead
- PC hook tracking via std::vector<unsigned int> for instruction verification

## Important Implementation Details

### Memory System
The emulator has a two-tier memory system:
1. **Memory Regions** (via `add_region`): Pre-allocated memory blocks mapped to specific addresses, checked first
2. **Callback Functions**: If no region matches, falls back to read_mem/write_mem callbacks

### Hook System Architecture

The emulator provides two complementary hook systems:

#### 1. PC Hooks (Legacy, JavaScript-friendly)
- **Function**: `_set_pc_hook_func(callback)` - Sets a PC-only hook callback
- **Callback Signature**: `int hook(unsigned int pc)`
- **Filtering**: Use `_add_pc_hook_addr(address)` to hook specific addresses only
  - Empty filter set = hook ALL addresses (default behavior)
  - Non-empty filter set = hook ONLY specified addresses
- **Usage**: Simple JavaScript callbacks, breakpoint-style debugging
- **JavaScript Integration**: Works with `_set_probe_callback()` for WASM environments

#### 2. Full Instruction Hooks (Advanced, C/C++ friendly)
- **Function**: `_set_full_instr_hook_func(callback)` - Sets a full instruction hook callback
- **Callback Signature**: `int hook(unsigned int pc, unsigned int ir, unsigned int cycles)`
  - `pc`: Program counter (instruction address)
  - `ir`: Instruction register (opcode)
  - `cycles`: Cycle count for this instruction
- **Usage**: Detailed instruction analysis, performance profiling, advanced debugging
- **No Filtering**: Always called for every instruction (use internal logic to filter)

#### Hook System Implementation
- All hooks are called via `m68k_instruction_hook_wrapper()` before each instruction
- Return 0 to continue execution, non-zero to break out of execution loop
- Hooks are called in this order: Tracing → Full Instruction Hook → PC Hook/JavaScript Probe
- **Critical for Testing**: PC hooks are the most reliable way to verify instruction execution
- Hook functions can call `m68k_end_timeslice()` to stop execution gracefully

#### Clearing Hooks
- `_clear_pc_hook_func()` - Removes PC hook callback
- `_clear_instr_hook_func()` - Removes full instruction hook callback
- `_clear_pc_hook_addrs()` - Clears PC address filter (reverts to "hook all")
- `_reset_myfunc_state()` - Resets all hooks and callbacks to initial state

### CPU Execution Model
- `m68k_execute(cycles)` runs instructions until cycle count exhausted
- The CPU maintains internal cycle counting - instructions consume varying cycles
- After reset, PC is loaded from address 4, SP from address 0
- The instruction hook can interrupt execution early by returning non-zero

### myfunc.cc API Layer
This C++ wrapper provides the WebAssembly interface:
- Manages callback function pointers (read_mem, write_mem, pc_hook)
- Implements memory region system (Region class does NOT own memory - caller responsible for cleanup)
- All callbacks are NULL-checked before invocation to prevent crashes
- Uses std::vector for regions and std::unordered_set for PC hook addresses
- Provides `clear_regions()` for test cleanup between test cases

### Critical Files Relationship
- **m68kconf.h**: Configures CPU features and hooks - changes here affect entire emulation
- **m68kcpu.c**: Includes m68kfpu.c directly (not compiled separately)
- **myfunc.cc**: C++ API layer with STL containers for regions and callbacks
- **build.fish**: Authoritative WASM build script with all Emscripten flags

### WebAssembly Build Specifics
The Fish script build process:
1. Runs `emmake make -j20` to build object files with Emscripten
2. Links with extensive exported functions list for JavaScript access
3. Generates two outputs: web version and Node.js version
4. Uses WASM_BIGINT for 64-bit number support in JavaScript
5. Enables ALLOW_MEMORY_GROWTH for dynamic memory allocation

#### Building with Perfetto Tracing Support
To enable Perfetto tracing in WebAssembly builds:

1. **Generate protobuf files first** (required for compilation):
   ```bash
   # Must be done before building with ENABLE_PERFETTO=1
   mkdir -p third_party/retrobus-perfetto/proto
   third_party/protobuf-3.21.12/build.host/protoc \
     --cpp_out=third_party/retrobus-perfetto/proto \
     -I third_party/retrobus-perfetto/proto \
     third_party/retrobus-perfetto/proto/perfetto.proto
   
   # Also copy to expected location for Makefile
   cp third_party/retrobus-perfetto/cpp/proto/perfetto.pb.* third_party/retrobus-perfetto/proto/
   ```

2. **Build with Perfetto enabled**:
   ```bash
   # Use the enhanced build script (recommended)
   ENABLE_PERFETTO=1 ./build.fish
   
   # Or manually with emmake
   emmake make -j8 ENABLE_PERFETTO=1
   ```

3. **Critical build requirements**:
   - Must include `m68k_memory_bridge.o` (provides memory access functions)
   - m68kfpu.c is included in m68kcpu.o (don't link separately)
   - Link order matters: protobuf before abseil libraries

4. **JavaScript/WASM Integration**:
   - Perfetto functions exported with `_m68k_perfetto_` prefix
   - Use `_m68k_perfetto_export_trace()` not `save_trace()` (WASM has no direct file access)
   - Trace data returned as pointer to WASM heap - must copy to JavaScript buffer
   - Always call `_m68k_perfetto_free_trace_data()` after export to prevent memory leaks

5. **Testing Perfetto in WASM**:
   ```javascript
   // Initialize Perfetto
   Module.ccall('m68k_perfetto_init', 'number', ['string'], ['ProcessName']);
   
   // Enable tracing modes
   Module._m68k_perfetto_enable_flow(1);
   Module._m68k_perfetto_enable_instructions(1);
   
   // Export trace data
   const dataPtrPtr = Module._malloc(4);
   const sizePtr = Module._malloc(4);
   Module._m68k_perfetto_export_trace(dataPtrPtr, sizePtr);
   
   // Copy trace data from WASM heap
   const dataPtr = Module.getValue(dataPtrPtr, '*');
   const dataSize = Module.getValue(sizePtr, 'i32');
   const traceData = new Uint8Array(Module.HEAPU8.buffer, dataPtr, dataSize);
   
   // Save to file (Node.js)
   fs.writeFileSync('trace.perfetto-trace', traceData);
   
   // Cleanup
   Module._m68k_perfetto_free_trace_data(dataPtr);
   ```

### GitHub Actions CI
- **native-ci.yml**: Tests CMake build on Ubuntu/macOS with sanitizer tests as critical (no continue-on-error)
- **wasm-ci.yml**: Tests Fish script WASM build and uploads artifacts
- **typescript-ci.yml**: Tests TypeScript wrapper and integration
- All M68k CPU tests are now enabled and passing (previously 7 of 9 were disabled)
- Sanitizer builds catch memory issues like alloc-dealloc mismatches and uninitialized reads

## Release Process

### Creating a Release
The project uses an automated release workflow that publishes to npm:

1. **Create a Git Tag**:
   ```bash
   # Tag should follow semantic versioning (v1.2.3)
   git tag v0.1.2
   git push origin v0.1.2
   ```

2. **Create GitHub Release**:
   - Go to GitHub Releases page
   - Click "Create a new release"
   - Select the tag created above
   - Add release notes describing changes
   - Click "Publish release"

3. **Automated npm Publishing**:
   - The `npm-publish.yml` workflow automatically triggers on release publication
   - Downloads artifacts from the latest successful WebAssembly CI run
   - Creates npm package with both standard and Perfetto builds
   - Publishes to npm as `musashi-wasm` package

### Release Workflow Features
- **Artifact Reuse**: Downloads pre-built WASM files from CI instead of rebuilding
- **Dual Builds**: Packages both standard and Perfetto-enabled WASM modules
- **ESM-Only**: Publishes modern ES module format (`.mjs` files)
- **Provenance**: Includes npm provenance for supply chain security
- **Manual Trigger**: Supports manual workflow dispatch for republishing

### NPM Package Structure
```
musashi-wasm/
├── dist/
│   ├── musashi.wasm              # Standard build
│   ├── musashi-loader.mjs        # Standard ESM loader
│   ├── musashi-perfetto.wasm     # Perfetto build
│   └── musashi-perfetto-loader.mjs # Perfetto ESM loader
├── index.mjs                     # Standard build entry point
├── perf.mjs                      # Perfetto build entry point
└── package.json                  # ESM-only configuration
```

### Release Prerequisites
- **NPM_TOKEN**: Must be configured in GitHub repository secrets
- **WebAssembly CI**: Must complete successfully for the release commit
- **Semantic Versioning**: Release tags must follow `v1.2.3` format

## Troubleshooting

### Hook System Issues

#### PC Hooks Not Firing
**Problem**: PC hook callbacks are not being called during execution.

**Common Causes**:
1. **Memory callbacks not set**: The CPU requires memory read/write callbacks before execution
2. **Hook callback not registered**: Use `_set_pc_hook_func()` to register the callback
3. **Address filtering active**: Check if `_add_pc_hook_addr()` was called with specific addresses
4. **CPU not executing**: Ensure `m68k_execute(cycles)` is called with sufficient cycles (>10)

**Debug Steps**:
```javascript
// Enable debug logging
Module._enable_printf_logging();

// Verify callback is set
console.log("Setting PC hook...");
Module._set_pc_hook_func(Module.addFunction(myHook, 'ii'));

// Clear any address filters (hook all addresses)
Module._clear_pc_hook_addrs();

// Ensure memory callbacks are set
Module._set_read8_callback(Module.addFunction(readMem, 'ii'));
Module._set_write8_callback(Module.addFunction(writeMem, 'vii'));
```

#### Instruction Hooks vs PC Hooks Confusion
**Problem**: Unclear which hook system to use.

**Guidelines**:
- Use **PC Hooks** (`_set_pc_hook_func`) for:
  - JavaScript/WASM integration
  - Simple breakpoint-style debugging
  - Address-filtered monitoring
- Use **Full Instruction Hooks** (`_set_full_instr_hook_func`) for:
  - C/C++ native code
  - Detailed instruction analysis (opcode + cycle data)
  - Performance profiling

### Build Issues

#### Emscripten Not Found
**Problem**: `emcc not found` or `EMSDK not set` errors.

**Solutions**:
1. **Use build.fish**: The enhanced script auto-detects Emscripten installations
2. **Check EMSDK**: Ensure environment variable points to correct directory
3. **Homebrew users**: Script automatically detects Homebrew Emscripten installations
4. **Manual PATH**: Add Emscripten tools to PATH before running make

#### Fish Shell PATH Issues
**Problem**: PATH not inherited correctly in Fish shell.

**Solution**: Use `build.fish` which handles PATH management automatically:
```bash
# Handles both standard and Homebrew EMSDK installations
./build.fish
```

### Test Issues

#### Tests Failing on macOS ARM64
**Problem**: GoogleTest linking errors on Apple Silicon.

**Workarounds**:
1. Run tests on Linux (CI environment)
2. Use system GoogleTest: `brew install googletest`
3. Build without tests: skip `-DBUILD_TESTS=ON`

#### Memory Access Violations in Tests
**Problem**: Sanitizer or segmentation errors during tests.

**Debug Steps**:
1. **Enable sanitizers**: Use `-DENABLE_SANITIZERS=ON` in CMake
2. **Check memory callbacks**: Ensure read/write callbacks are set before `m68k_execute()`
3. **Clear state between tests**: Call `reset_myfunc_state()` in test teardown
4. **Verify memory regions**: Use `clear_regions()` to reset memory mappings

### Performance Issues

#### Hooks Causing Slowdown
**Problem**: Hook callbacks significantly slow down emulation.

**Solutions**:
1. **Use address filtering**: `_add_pc_hook_addr()` to limit hook scope
2. **Optimize callback code**: Minimize work in hook functions
3. **Disable hooks when not needed**: Use `_clear_pc_hook_func()` 
4. **Use instruction hooks judiciously**: Full hooks provide more data but have higher overhead

## Known Issues

### GoogleTest Linking Issue on macOS ARM64
There is a known linking issue with GoogleTest on macOS ARM64 (Apple Silicon) systems that prevents tests from building locally. The issue manifests as:
```
Undefined symbols for architecture arm64:
  "testing::internal::MakeAndRegisterTestInfo(std::__1::basic_string<char, ...>)"
```

**Root Cause**: C++ ABI mismatch between GoogleTest and test code:
- Test code expects `std::__1::basic_string` (libc++, LLVM's standard library)
- GoogleTest provides `char const*` signatures
- This is due to different C++ standard library implementations or compilation flags

**Attempted Solutions**:
- Added `-stdlib=libc++` flags for consistency (already in CMakeLists.txt)
- Tested GoogleTest versions: v1.14.0, v1.13.0, v1.12.1, and recommended commit
- Issue is pre-existing and affects all test targets, not just Perfetto tests

**Workarounds**:
1. Build and run tests on Linux (GitHub Actions CI works correctly)
2. Use system-installed GoogleTest: `brew install googletest` and use `find_package(GTest REQUIRED)`
3. Consider alternative testing frameworks like Catch2 for local development

**References**:
- [GoogleTest Issue #2021](https://github.com/google/googletest/issues/2021)
- [GoogleTest Issue #3802](https://github.com/google/googletest/issues/3802) (Apple M1 specific)
- [Stack Overflow: GoogleTest undefined symbols](https://stackoverflow.com/questions/38802547/googletest-undefined-symbols-for-architecture-x86-64-error)