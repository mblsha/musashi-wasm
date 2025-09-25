# musashi-wasm

WebAssembly port of the Musashi M68000 CPU emulator with optional Perfetto tracing support.

## Features

- Full M68000/68010/68020/68030/68040 CPU emulation
- WebAssembly for high performance in Node.js and browsers
- Optional Perfetto tracing for performance analysis
- TypeScript support with complete type definitions
- Memory-mapped I/O support
- Customizable memory access callbacks
- PC hook for instruction-level debugging

## Installation

```bash
npm install musashi-wasm
```

## Quick Start

### Basic Usage (Standard Build)

```javascript
import Musashi from 'musashi-wasm';

async function runEmulator() {
  const cpu = new Musashi();
  await cpu.init();
  
  // Allocate 1MB of memory
  const memSize = 1024 * 1024;
  const memPtr = cpu.allocateMemory(memSize);
  const memory = new Uint8Array(memSize);
  
  // Set up memory callbacks
  cpu.setReadMemFunc((address) => {
    return memory[address & (memSize - 1)];
  });
  
  cpu.setWriteMemFunc((address, value) => {
    memory[address & (memSize - 1)] = value;
  });
  
  // Write reset vectors
  const resetSP = 0x00001000;  // Stack pointer
  const resetPC = 0x00000400;  // Program counter
  
  // Write to memory (big-endian)
  memory[0] = (resetSP >> 24) & 0xFF;
  memory[1] = (resetSP >> 16) & 0xFF;
  memory[2] = (resetSP >> 8) & 0xFF;
  memory[3] = resetSP & 0xFF;
  memory[4] = (resetPC >> 24) & 0xFF;
  memory[5] = (resetPC >> 16) & 0xFF;
  memory[6] = (resetPC >> 8) & 0xFF;
  memory[7] = resetPC & 0xFF;
  
  // Write program at resetPC (NOP instruction: 0x4E71)
  memory[resetPC] = 0x4E;
  memory[resetPC + 1] = 0x71;
  
  // Copy memory to WASM heap
  cpu.writeMemory(memPtr, memory);
  cpu.addRegion(0, memSize, memPtr);
  
  // Reset and run CPU
  cpu.pulseReset();
  const cyclesExecuted = cpu.execute(1000);
  
  console.log(`Executed ${cyclesExecuted} cycles`);
  console.log(`PC: 0x${cpu.getReg(16).toString(16)}`); // 16 = PC register
  
  // Cleanup
  cpu.clearRegions();
  cpu.freeMemory(memPtr);
}

runEmulator().catch(console.error);
```

### With Perfetto Tracing

```javascript
import { MusashiPerfetto } from 'musashi-wasm/perfetto';

async function runWithTracing() {
  const cpu = new MusashiPerfetto();
  await cpu.init('MyEmulator');
  
  // Enable tracing modes
  cpu.enableFlowTracing(true);
  cpu.enableInstructionTracing(true);
  cpu.enableMemoryTracing(true);
  
  // ... set up memory and run emulation ...
  
  // Export trace
  const traceData = await cpu.exportTrace();
  if (traceData) {
    // Save to file (Node.js only)
    if (typeof process !== 'undefined' && process.versions?.node) {
      const { writeFileSync } = await import('fs');
      writeFileSync('trace.perfetto-trace', traceData);
      console.log('Trace saved to trace.perfetto-trace');
    }
    // Open in https://ui.perfetto.dev
  }
}
```

### ES Modules

The package exports ESM wrappers that work in both Node and browsers with the same import paths.

```javascript
import Musashi from 'musashi-wasm';
import { MusashiPerfetto } from 'musashi-wasm/perfetto';

const cpu = new Musashi();
await cpu.init();
// ...
```

### TypeScript

```typescript
import Musashi, { M68kRegister } from 'musashi-wasm';

const cpu = new Musashi();
await cpu.init();

// TypeScript knows the register enum
const pc = cpu.getReg(M68kRegister.PC);
const d0 = cpu.getReg(M68kRegister.D0);
```

### Core wrapper (fusion runtime)

For Node/tooling use cases, `musashi-wasm` exposes the core wrapper under a stable subpath:

```ts
import {
  createSystem,
  M68kRegister,
  type System,
  type SystemConfig,
} from 'musashi-wasm/core';

// ...
```

The raw Emscripten factory remains available at `musashi-wasm/node`:

```ts
import initMusashi from 'musashi-wasm/node';
const mod = await initMusashi();
```

This avoids vendoring the wrapper in downstream projects and ensures shared enums/types
(e.g., `M68kRegister`, `SystemConfig`, `System`) are available from the same package.

## API Reference

### Class: Musashi

#### Methods

- `async init()`: Initialize the emulator (must be called first)
- `execute(cycles: number): number`: Execute instructions for specified cycles
- `pulseReset()`: Reset the CPU
- `cyclesRun(): number`: Get total cycles executed
- `getReg(reg: M68kRegister): number`: Read CPU register
- `setReg(reg: M68kRegister, value: number)`: Write CPU register
- `addRegion(base: number, size: number, dataPtr: number)`: Map memory region
- `clearRegions()`: Clear all memory regions
- `setReadMemFunc(func: (address: number) => number): number`: Set memory read callback
- `setWriteMemFunc(func: (address: number, value: number) => void): number`: Set memory write callback
- `setPCHookFunc(func: (address: number) => number): number`: Set PC hook callback
- `removeFunction(ptr: number)`: Remove callback function
- `allocateMemory(size: number): number`: Allocate WASM heap memory
- `freeMemory(ptr: number)`: Free WASM heap memory
- `writeMemory(ptr: number, data: Uint8Array)`: Write to WASM heap
- `readMemory(ptr: number, size: number): Uint8Array`: Read from WASM heap
- `enablePrintfLogging(enable: boolean)`: Enable debug logging

### Class: MusashiPerfetto

Extends `Musashi` with additional methods:

- `async init(processName?: string)`: Initialize with Perfetto support
- `enableFlowTracing(enable: boolean)`: Enable control flow tracing
- `enableInstructionTracing(enable: boolean)`: Enable instruction tracing
- `enableMemoryTracing(enable: boolean)`: Enable memory access tracing
- `enableInterruptTracing(enable: boolean)`: Enable interrupt tracing
- `async exportTrace(): Uint8Array | null`: Export trace data
- `async saveTrace(filename: string): boolean`: Save trace to file

### Enum: M68kRegister

`M68kRegister` is provided by `@m68k/common` and re-exported by this package.

## Advanced Examples

### Loading Binary Programs

```javascript
import Musashi from 'musashi-wasm';
import { readFileSync } from 'fs';

async function loadAndRun(programPath) {
  const cpu = new Musashi();
  await cpu.init();
  
  // Read program
  const program = readFileSync(programPath);
  
  // Allocate memory
  const memSize = 1024 * 1024;
  const memPtr = cpu.allocateMemory(memSize);
  const memory = new Uint8Array(memSize);
  
  // Set up reset vectors
  const resetSP = 0x00100000;
  const resetPC = 0x00001000;
  
  // Write vectors (big-endian)
  const view = new DataView(memory.buffer);
  view.setUint32(0, resetSP, false);
  view.setUint32(4, resetPC, false);
  
  // Copy program to memory
  memory.set(program, resetPC);
  
  // Set up memory access
  cpu.setReadMemFunc((address) => {
    if (address < memSize) {
      return memory[address];
    }
    return 0xFF; // Unmapped memory
  });
  
  cpu.setWriteMemFunc((address, value) => {
    if (address < memSize) {
      memory[address] = value;
    }
  });
  
  // Copy to WASM and map
  cpu.writeMemory(memPtr, memory);
  cpu.addRegion(0, memSize, memPtr);
  
  // Run
  cpu.pulseReset();
  
  // Execute with PC hook to detect infinite loops
  let lastPC = -1;
  let sameCount = 0;
  
  cpu.setPCHookFunc((pc) => {
    if (pc === lastPC) {
      sameCount++;
      if (sameCount > 1000) {
        console.log(`Detected infinite loop at PC: 0x${pc.toString(16)}`);
        return 1; // Stop execution
      }
    } else {
      sameCount = 0;
      lastPC = pc;
    }
    return 0; // Continue
  });
  
  // Run for up to 1 million cycles
  const cycles = cpu.execute(1000000);
  console.log(`Program executed ${cycles} cycles`);
  
  // Cleanup
  cpu.clearRegions();
  cpu.freeMemory(memPtr);
}

loadAndRun('./program.bin').catch(console.error);
```

### Memory-Mapped I/O

```javascript
async function setupMMIO() {
  const cpu = new Musashi();
  await cpu.init();
  
  // Define I/O region
  const IO_BASE = 0xFF0000;
  const IO_SIZE = 0x10000;
  
  // I/O registers
  const ioRegisters = new Uint8Array(256);
  
  cpu.setReadMemFunc((address) => {
    if (address >= IO_BASE && address < IO_BASE + IO_SIZE) {
      const offset = address - IO_BASE;
      
      // Handle specific I/O addresses
      switch (offset) {
        case 0x00: // Status register
          return 0x80; // Ready bit
        case 0x04: // Data register
          return ioRegisters[offset];
        default:
          return 0xFF;
      }
    }
    
    // Regular memory access
    return memory[address];
  });
  
  cpu.setWriteMemFunc((address, value) => {
    if (address >= IO_BASE && address < IO_BASE + IO_SIZE) {
      const offset = address - IO_BASE;
      
      // Handle I/O writes
      switch (offset) {
        case 0x04: // Data register
          console.log(`I/O Write: ${String.fromCharCode(value)}`);
          ioRegisters[offset] = value;
          break;
        case 0x08: // Control register
          if (value & 0x01) {
            console.log('Device enabled');
          }
          break;
      }
      return;
    }
    
    // Regular memory write
    memory[address] = value;
  });
  
  // ... rest of setup
}
```

## Performance Tips

1. **Use Memory Regions**: Pre-mapped memory regions are faster than callbacks
2. **Batch Operations**: Minimize calls between JavaScript and WASM
3. **Reuse Buffers**: Allocate WASM memory once and reuse
4. **Profile with Perfetto**: Use tracing to identify bottlenecks

## Building from Source

If you need to build the WASM modules yourself:

```bash
# From the repository root
npm install

# Build WebAssembly (standard)
./build.sh

# Build WebAssembly with Perfetto
ENABLE_PERFETTO=1 ./build.sh

# Generate npm wrapper files
cd npm-package
npm run build
```

## Compatibility

- Node.js: 16+ (ESM)
- Browsers: Modern browsers with WebAssembly support
- TypeScript: Full type definitions included

## License

MIT - See LICENSE file for details

## Contributing

Contributions are welcome! Please see CONTRIBUTING.md for guidelines.

## Support

- Issues: https://github.com/yourusername/Musashi/issues
- Discussions: https://github.com/yourusername/Musashi/discussions

## Credits

Based on the Musashi M68000 emulator by Karl Stenerud.
