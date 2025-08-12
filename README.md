# Musashi M68k Emulator - WebAssembly Edition

[![WebAssembly CI](https://github.com/mblsha/Musashi/actions/workflows/wasm-ci.yml/badge.svg)](https://github.com/mblsha/Musashi/actions/workflows/wasm-ci.yml)
[![TypeScript CI](https://github.com/mblsha/Musashi/actions/workflows/typescript-ci.yml/badge.svg)](https://github.com/mblsha/Musashi/actions/workflows/typescript-ci.yml)
[![Native CI](https://github.com/mblsha/Musashi/actions/workflows/native-ci.yml/badge.svg)](https://github.com/mblsha/Musashi/actions/workflows/native-ci.yml)

A Motorola M68000 CPU emulator (v4.10) compiled to WebAssembly with TypeScript bindings and optional Perfetto tracing support.

## Features

- **M68k CPU Emulation**: Supports 68000, 68010, 68EC020, 68020, 68EC030, 68030, 68EC040, 68040
- **WebAssembly Target**: Runs in browsers and Node.js
- **TypeScript API**: Clean, type-safe interface via `@m68k/core` and `@m68k/memory` packages
- **Perfetto Tracing**: Optional performance profiling with Chrome's tracing format
- **Memory Hooks**: Custom memory access handlers
- **PC Hooks**: Breakpoint and code patching support

## Quick Start

### Using the TypeScript API

```typescript
import { createSystem } from '@m68k/core';

// Create a system with ROM and RAM
const system = await createSystem({
  rom: romData,          // Uint8Array with ROM contents
  ramSize: 64 * 1024    // 64KB of RAM
});

// Execute code
system.reset();
const cycles = await system.run(1000);

// Access registers
const regs = system.getRegisters();
console.log(`D0: 0x${regs.d0.toString(16)}`);
```

### Building from Source

```bash
# Build WebAssembly modules
make wasm

# Or with Perfetto tracing support
ENABLE_PERFETTO=1 ./build.fish

# Build and test TypeScript packages
npm install
npm run build
npm test
```

## TypeScript Packages

The TypeScript API is organized into modular packages:

- **[@m68k/core](packages/core)** - Core emulator with CPU control, memory access, and hooks
- **[@m68k/memory](packages/memory)** - Memory utilities for structured data access

See [packages/README.md](packages/README.md) for detailed documentation.

## Perfetto Tracing

When built with `ENABLE_PERFETTO=1`, the emulator can generate detailed performance traces:

```typescript
if (system.tracer.isAvailable()) {
  // Register symbols for better readability
  system.tracer.registerFunctionNames({
    0x400: 'main',
    0x500: 'game_loop'
  });

  // Start tracing
  system.tracer.start({
    instructions: true,  // Trace every instruction
    flow: true,         // Trace function calls/returns
    memory: true        // Trace memory writes
  });

  // Run your code
  await system.run(100000);

  // Export trace
  const traceData = await system.tracer.stop();
  fs.writeFileSync('trace.perfetto-trace', traceData);
}
```

View traces at [ui.perfetto.dev](https://ui.perfetto.dev).

## Project Structure

```
├── m68k*.c/h           # Core M68k emulation (C)
├── myfunc.cc           # WebAssembly API layer
├── m68ktrace.cc        # Tracing infrastructure
├── m68k_perfetto.cc    # Perfetto integration
├── build.fish          # WebAssembly build script
├── packages/           # TypeScript packages
│   ├── core/          # @m68k/core package
│   └── memory/        # @m68k/memory package
└── musashi-wasm-test/ # Integration tests
```

## Development

### Prerequisites

- Emscripten SDK (for WebAssembly builds)
- Node.js 16+ and npm
- CMake (for native builds)
- Fish shell (for build script)

### CI/CD

The project uses GitHub Actions for continuous integration:

- **WebAssembly CI**: Builds WASM modules with and without Perfetto
- **TypeScript CI**: Tests TypeScript packages with multiple Node versions
- **Native CI**: Tests native builds on Ubuntu and macOS

### Testing

```bash
# Run all tests
npm test

# Run specific package tests
npm test --workspace=@m68k/core
npm test --workspace=@m68k/memory

# Run integration tests
cd musashi-wasm-test
npm test
```

## License

See individual source files for licensing information. The original Musashi emulator was created by Karl Stenerud.

## Contributing

Contributions are welcome! Please ensure:
- All tests pass (`npm test`)
- TypeScript types check (`npm run typecheck`)
- CI workflows pass

## Resources

- [Musashi Original](https://github.com/kstenerud/Musashi) - Original M68k emulator
- [M68k Reference](https://www.nxp.com/docs/en/reference-manual/M68000PRM.pdf) - Motorola 68000 Programmer's Reference
- [Perfetto UI](https://ui.perfetto.dev) - Trace viewer
- [Emscripten](https://emscripten.org) - C/C++ to WebAssembly compiler