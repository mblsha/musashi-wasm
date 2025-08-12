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

## Requirements

- Node.js 16+ or modern browser
- The WASM module must be built first: `make wasm`
- For Perfetto support: `ENABLE_PERFETTO=1 ./build.fish`