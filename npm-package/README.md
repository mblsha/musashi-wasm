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
const Musashi = require('musashi-wasm');

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
const MusashiPerfetto = require('musashi-wasm/perfetto');

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
    // Save to file (Node.js)
    const fs = require('fs');
    fs.writeFileSync('trace.perfetto-trace', traceData);
    console.log('Trace saved to trace.perfetto-trace');
    // Open in https://ui.perfetto.dev
  }
}
```

### ES Modules

```javascript
import Musashi from 'musashi-wasm';
// or
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

```typescript
enum M68kRegister {
  D0 = 0, D1 = 1, D2 = 2, D3 = 3, D4 = 4, D5 = 5, D6 = 6, D7 = 7,
  A0 = 8, A1 = 9, A2 = 10, A3 = 11, A4 = 12, A5 = 13, A6 = 14, A7 = 15,
  PC = 16,   // Program Counter
  SR = 17,   // Status Register
  SP = 18,   // Stack Pointer
  USP = 19,  // User Stack Pointer
  ISP = 20,  // Interrupt Stack Pointer
  MSP = 21,  // Master Stack Pointer
  // ... additional registers
}
```

## Advanced Examples

### Loading Binary Programs

```javascript
const fs = require('fs');
const Musashi = require('musashi-wasm');

async function loadAndRun(programPath) {
  const cpu = new Musashi();
  await cpu.init();
  
  // Read program
  const program = fs.readFileSync(programPath);
  
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
# Clone the repository
git clone https://github.com/yourusername/Musashi.git
cd Musashi

# Install Emscripten
# See: https://emscripten.org/docs/getting_started/downloads.html

# Build standard version
./build_wasm_simple.sh

# Build Perfetto version
./build_perfetto_wasm_simple.sh

# Generate npm package
cd npm-package
npm run build:lib
```

## Compatibility

- Node.js: 14.0.0 or higher
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