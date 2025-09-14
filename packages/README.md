# M68k TypeScript Packages

This directory contains TypeScript packages for the M68k emulator with optional Perfetto tracing support.

## Packages

### @m68k/core

The core M68k emulator package that provides:
- Low-level CPU emulation via WebAssembly
- Memory access (read/write)
- Register manipulation
- Execution control (run, call, reset)
- Hook system (probe and override)
- Hook system (`addHook` unified API; `probe` and `override` retained for compatibility)
- Optional Perfetto tracing support

### @m68k/memory

Memory utilities for structured access to M68k memory:
- `MemoryRegion`: Type-safe access to fixed memory structures
- `MemoryArray`: Array-like access to collections of structures
- `DataParser`: Utilities for parsing big-endian data

## Quick Start

```typescript
import { createSystem } from '@m68k/core';
import { MemoryRegion, DataParser } from '@m68k/memory';

// Create a system with ROM and RAM
const system = await createSystem({
  rom: romData, // Uint8Array with ROM contents
  ramSize: 64 * 1024 // 64KB of RAM
});

// Execute code
system.reset();
const cycles = await system.run(1000);

// Access registers
const regs = system.getRegisters();
console.log(`PC: 0x${regs.pc.toString(16)}`);

// Use memory utilities
const statusReg = new MemoryRegion(
  system,
  0x100000, // address
  4,        // size
  data => DataParser.readUint32BE(data)
);

const status = statusReg.get();
```

### Single-step execution

Execute exactly one instruction and inspect precise PCs and cycles:

```ts
const { cycles, startPc, endPc, ppc } = await system.step();
console.log(`Stepped ${cycles} cycles from 0x${startPc.toString(16)} to 0x${endPc.toString(16)}`);
// ppc is provided by the core and usually equals startPc
```

## Perfetto Tracing

If the WASM module is built with Perfetto support (`ENABLE_PERFETTO=1`), you can capture performance traces:

```typescript
const system = await createSystem({ rom, ramSize });

// Check if tracing is available
if (system.tracer.isAvailable()) {
  // Register symbol names for better trace readability
  system.tracer.registerFunctionNames({
    0x400: 'main',
    0x500: 'update_loop',
    0x600: 'render'
  });

  // Start tracing
  system.tracer.start({
    instructions: true,  // Trace every instruction
    flow: true,         // Trace function calls/returns
    memory: true        // Trace memory writes
  });

  // Run your code
  await system.run(100000);

  // Stop and export trace
  const traceData = await system.tracer.stop();
  
  // Save to file (Node.js)
  fs.writeFileSync('trace.perfetto-trace', traceData);
}
```

View the trace at [ui.perfetto.dev](https://ui.perfetto.dev).

## New APIs (ergonomics)

- Unified PC hooks: `system.addHook(address, sys => 'continue' | 'stop')`. Use `'stop'` to halt execution (useful for `call()` and stepping). `probe` and `override` remain available but are superseded by `addHook`.
- Rich disassembly: `system.disassembleDetailed(pc)` returns `{ text, size }` in one call while `disassemble(pc)` keeps returning the formatted text.
- Cleanup: `system.dispose()` to release underlying WASM callbacks/regions when done.

## Migration Guide

This release adds a few ergonomic APIs while keeping backwards compatibility. Suggested migrations:

- Unified PC hooks
  - Before: `probe(address, cb)` to continue; `override(address, cb)` to stop and emulate RTS.
  - After: `addHook(address, sys => 'continue' | 'stop')`.
    - Equivalent mappings:
      - `probe(addr, cb)` → `addHook(addr, sys => { cb(sys); return 'continue'; })`
      - `override(addr, cb)` → `addHook(addr, sys => { cb(sys); return 'stop'; })`
    - `probe`/`override` remain and will be deprecated in a future minor release.

- Disassembly helpers
  - Before: `disassemble(pc)` for text and `getInstructionSize(pc)` for size.
  - After: `disassembleDetailed(pc)` returns `{ text, size }` in one call.
    - You can keep `disassemble(pc)` if you only need text; `getInstructionSize(pc)` stays for compatibility.

- Lifecycle cleanup
  - New: `system.dispose()` tears down WASM callbacks/regions. Call it when the system is no longer needed (tests, short‑lived tools).

- npm wrapper enum correction
  - If you consume `musashi-wasm` directly, the `M68kRegister` enum now matches Musashi headers. `PPC` is `19` (was incorrectly `29`). Update code that depends on raw enum values.

### Backwards compatibility and timeline

- These changes are additive. Existing code continues to work.
- In a future minor, `probe`/`override` will be marked `@deprecated` in types and docs.
- In a future major, we plan to prefer `@m68k/core` as the primary API and reduce duplication in the standalone npm wrapper.

## Building

From the repository root:

```bash
# Install dependencies
npm install

# Build TypeScript packages
npm run build

# Run tests
npm test
```

## Local Development with Real WASM

For local development and testing with real WASM artifacts, use the comprehensive test script:

```bash
# From repository root - full build and test
./test_with_real_wasm.sh

# With Perfetto tracing support
ENABLE_PERFETTO=1 ./test_with_real_wasm.sh

# Quick test with existing WASM (skip rebuild)
SKIP_WASM_BUILD=1 ./test_with_real_wasm.sh

# Verbose output for debugging
VERBOSE=1 ./test_with_real_wasm.sh

# View all options
./test_with_real_wasm.sh --help
```

This script automatically:
1. Builds WASM modules using `./build.fish` 
2. Copies artifacts to `packages/core/wasm/`
3. Runs TypeScript tests in `packages/core/`
4. Runs integration tests in `musashi-wasm-test/`
5. Provides detailed progress reporting and error handling

### Environment Variables

- `ENABLE_PERFETTO=1` - Enable Perfetto tracing support
- `SKIP_WASM_BUILD=1` - Skip WASM build step (use existing artifacts)
- `SKIP_CORE_TESTS=1` - Skip packages/core tests
- `SKIP_INTEGRATION=1` - Skip musashi-wasm-test integration tests  
- `VERBOSE=1` - Show detailed output for debugging

### Prerequisites

- Emscripten SDK (EMSDK) installed and activated
- Node.js 16+ 
- Fish shell (for build.fish script)
- For Perfetto: protobuf dependencies (built automatically if needed)

## Requirements

- Node.js 16+ or modern browser
- The WASM module must be built first: `make wasm`
- For Perfetto support: `ENABLE_PERFETTO=1 ./build.fish`
