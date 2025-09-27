# M68k TypeScript Packages

This directory contains TypeScript packages for the M68k emulator with optional Perfetto tracing support.

> **Heads-up:** `musashi-wasm/core` and `musashi-wasm/memory` are distributed prebuilt on npm. Git checkouts only include the TypeScript sources plus the WASM assets; `packages/*/dist` and `npm-package/lib/**` stay untracked by design. To exercise the bundled entrypoints locally, install the published package (`npm install musashi-wasm`) or regenerate the wrapper after cloning via `npm --prefix npm-package run build` / `./run-tests-ci.sh`.

## Packages

### musashi-wasm/core

Bundled runtime exported from the npm package (`packages/core` workspace is published internally as `@m68k/core`). It provides:
- Low-level CPU emulation via WebAssembly
- Memory access (read/write)
- Register manipulation
- Execution control (run, call, reset)
- Hook system (probe and override)
- Optional Perfetto tracing support

### musashi-wasm/memory

Memory utilities bundled with the npm package (`packages/memory` workspace name is `@m68k/memory`):
- `MemoryRegion`: Type-safe access to fixed memory structures
- `MemoryArray`: Array-like access to collections of structures
- `DataParser`: Utilities for parsing big-endian data

## Quick Start

```typescript
import { createSystem } from 'musashi-wasm/core';
import { MemoryRegion, DataParser } from 'musashi-wasm/memory';

// Create a system with ROM and RAM
const system = await createSystem({
  rom: romData, // Uint8Array with ROM contents
  ramSize: 64 * 1024 // 64KB of RAM
});

// Execute code
system.reset();
const cycles = system.run(1000);

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
const { cycles, startPc, endPc, ppc } = system.step();
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
  system.run(100000);

  // Stop and export trace
  const traceData = system.tracer.stop();
  
  // Save to file (Node.js)
  fs.writeFileSync('trace.perfetto-trace', traceData);
}
```

View the trace at [ui.perfetto.dev](https://ui.perfetto.dev).

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
1. Builds WASM modules using `./build.sh` 
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
- Bash shell (build.sh)
- For Perfetto: protobuf dependencies (built automatically if needed)

## Requirements

- Node.js 16+ or modern browser
- The WASM module must be built first: `make wasm`
- For Perfetto support: `ENABLE_PERFETTO=1 ./build.sh`
