# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview
Musashi is a Motorola M68000 CPU emulator (v4.10) modified to compile to WebAssembly. The project emulates various M68k variants (68000, 68010, 68EC020, 68020, 68EC030, 68030, 68EC040, 68040) and provides a JavaScript API for web and Node.js environments.

## Build Commands

### Standard Build
```bash
# Build C object files and generate M68k instruction tables
make

# Build WebAssembly modules for both web and Node.js
make wasm
```

### Clean Build
```bash
make clean
make wasm  # Rebuilds WASM modules
```

## Architecture

### Code Generation Flow
1. `m68kmake.c` reads `m68k_in.c` (instruction definitions) and generates:
   - `m68kops.c` - Implementation of all M68k instructions
   - `m68kops.h` - Headers for instruction handlers
2. Core emulation in `m68kcpu.c` uses the generated instruction handlers
3. `myfunc.c` provides WebAssembly-specific API and memory management

### Key Components
- **CPU Core**: `m68kcpu.c/h` - Main emulation logic, register management, execution loop
- **Instruction Generator**: `m68kmake.c` + `m68k_in.c` - Generates opcode implementations
- **Disassembler**: `m68kdasm.c` - Converts machine code to assembly
- **FPU**: `m68kfpu.c` - Floating-point unit emulation using softfloat library
- **WASM Interface**: `myfunc.c` - Memory regions, callbacks, JavaScript interop

### WebAssembly Exports
Key functions exposed to JavaScript:
- CPU Control: `_m68k_init`, `_m68k_execute`, `_m68k_pulse_reset`, `_m68k_cycles_run`
- Register Access: `_m68k_get_reg`, `_m68k_set_reg`
- Memory Callbacks: `_set_read_mem_func`, `_set_write_mem_func`
- PC Hook: `_set_pc_hook_func` (called on every instruction)
- Memory Management: `_add_region` (maps memory regions)
- Debug: `_enable_printf_logging`

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

### Debugging Test Failures
The M68k CPU tests require proper initialization:
1. **Reset Vector Setup**: The CPU reads initial SP from address 0 and PC from address 4
2. **Memory Callbacks**: Must be set before CPU operations (read_mem, write_mem, pc_hook)
3. **Instruction Execution**: Use m68k_execute(cycles) with sufficient cycles (>10 for most instructions)

### Known Issues
- **Instruction Execution**: Currently the CPU doesn't advance PC properly during execution
- **Test Memory**: Tests use a 1MB memory buffer, ensure addresses stay within bounds
- **Sanitizers**: Use `-DENABLE_SANITIZERS=ON` for memory debugging on Linux/macOS

### Test Structure
Tests use Google Test framework with fixtures:
- `M68kTest`: Tests CPU core functionality
- `MyFuncTest`: Tests the myfunc.c API wrapper

Each test fixture sets up:
- Memory buffer (1MB)
- Memory access callbacks
- Reset vector at addresses 0-7 (SP at 0, PC at 4)
- CPU initialization with m68k_init() and m68k_pulse_reset()