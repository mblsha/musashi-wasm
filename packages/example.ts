#!/usr/bin/env node

/**
 * Example usage of the @m68k/core package with Perfetto tracing
 * 
 * This example demonstrates:
 * - Creating a system with ROM and RAM
 * - Loading and executing M68k machine code
 * - Using hooks to monitor execution
 * - Capturing Perfetto traces (if available)
 */

import { createSystem } from './core/src';
import { MemoryRegion, DataParser } from './memory/src';
import * as fs from 'fs';
import * as path from 'path';

async function main() {
  console.log('M68k Emulator Example\n');
  
  // Create a ROM with a simple test program
  const rom = new Uint8Array(4096);
  
  // Reset vectors
  // Stack pointer at 0x10000
  rom[0] = 0x00; rom[1] = 0x01; rom[2] = 0x00; rom[3] = 0x00;
  // Program counter at 0x400
  rom[4] = 0x00; rom[5] = 0x00; rom[6] = 0x04; rom[7] = 0x00;
  
  // Program at 0x400: Calculate factorial of 5
  let pc = 0x400;
  
  // MOVE.L #5, D0 (number to calculate factorial of)
  rom[pc++] = 0x20; rom[pc++] = 0x3C;
  rom[pc++] = 0x00; rom[pc++] = 0x00;
  rom[pc++] = 0x00; rom[pc++] = 0x05;
  
  // BSR factorial (0x420)
  rom[pc++] = 0x61; rom[pc++] = 0x00;
  rom[pc++] = 0x00; rom[pc++] = 0x18;
  
  // Store result to RAM
  // MOVE.L D0, $100000
  rom[pc++] = 0x23; rom[pc++] = 0xC0;
  rom[pc++] = 0x00; rom[pc++] = 0x10;
  rom[pc++] = 0x00; rom[pc++] = 0x00;
  
  // STOP #$2700
  rom[pc++] = 0x4E; rom[pc++] = 0x72;
  rom[pc++] = 0x27; rom[pc++] = 0x00;
  
  // Factorial subroutine at 0x420
  pc = 0x420;
  
  // CMP.L #1, D0
  rom[pc++] = 0x0C; rom[pc++] = 0x80;
  rom[pc++] = 0x00; rom[pc++] = 0x00;
  rom[pc++] = 0x00; rom[pc++] = 0x01;
  
  // BLE done (0x438)
  rom[pc++] = 0x6F; rom[pc++] = 0x00;
  rom[pc++] = 0x00; rom[pc++] = 0x10;
  
  // MOVE.L D0, -(SP)
  rom[pc++] = 0x2F; rom[pc++] = 0x00;
  
  // SUBQ.L #1, D0
  rom[pc++] = 0x53; rom[pc++] = 0x80;
  
  // BSR factorial (recursive call)
  rom[pc++] = 0x61; rom[pc++] = 0xF0;
  
  // MOVE.L (SP)+, D1
  rom[pc++] = 0x22; rom[pc++] = 0x1F;
  
  // MULU D1, D0
  rom[pc++] = 0xC0; rom[pc++] = 0xC1;
  
  // RTS
  rom[pc++] = 0x4E; rom[pc++] = 0x75;
  
  // done: at 0x438
  pc = 0x438;
  // MOVEQ #1, D0
  rom[pc++] = 0x70; rom[pc++] = 0x01;
  
  // RTS
  rom[pc++] = 0x4E; rom[pc++] = 0x75;
  
  // Create the system
  const system = await createSystem({
    rom,
    ramSize: 64 * 1024
  });
  
  console.log('System created successfully');
  
  // Set up execution hooks
  let instructionCount = 0;
  const removeProbe = system.probe(0x400, () => {
    console.log('Program started at 0x400');
  });
  
  const removeFactorialProbe = system.probe(0x420, (sys) => {
    const regs = sys.getRegisters();
    console.log(`Factorial called with D0 = ${regs.d0}`);
    instructionCount++;
  });
  
  // Check if Perfetto tracing is available
  let traceData: Uint8Array | null = null;
  
  if (system.tracer.isAvailable()) {
    console.log('\nPerfetto tracing is available!');
    
    // Register function names
    system.tracer.registerFunctionNames({
      0x400: 'main',
      0x420: 'factorial'
    });
    
    system.tracer.registerMemoryNames({
      0x100000: 'result_storage'
    });
    
    // Start tracing
    console.log('Starting trace...');
    system.tracer.start({
      instructions: true,
      flow: true,
      memory: true
    });
  } else {
    console.log('\nPerfetto tracing not available (rebuild with ENABLE_PERFETTO=1)');
  }
  
  // Reset and execute
  console.log('\nExecuting program...');
  system.reset();
  
  const cycles = system.run(10000);
  console.log(`\nExecution completed in ${cycles} cycles`);
  
  // Read the result from RAM
  const resultAddr = 0x100000;
  const result = system.read(resultAddr, 4);
  console.log(`\nFactorial(5) = ${result}`);
  
  // Verify the result
  if (result === 120) {
    console.log('✓ Result is correct!');
  } else {
    console.log(`✗ Expected 120, got ${result}`);
  }
  
  // Display registers
  const regs = system.getRegisters();
  console.log('\nFinal register state:');
  console.log(`  D0: 0x${regs.d0.toString(16).padStart(8, '0')}`);
  console.log(`  D1: 0x${regs.d1.toString(16).padStart(8, '0')}`);
  console.log(`  PC: 0x${regs.pc.toString(16).padStart(8, '0')}`);
  console.log(`  SP: 0x${regs.sp.toString(16).padStart(8, '0')}`);
  
  // Stop tracing and save if available
  if (system.tracer.isAvailable() && traceData === null) {
    try {
      console.log('\nStopping trace...');
      traceData = await system.tracer.stop();
      
      const traceFile = path.join(__dirname, 'example.perfetto-trace');
      fs.writeFileSync(traceFile, traceData);
      console.log(`Trace saved to: ${traceFile}`);
      console.log(`Trace size: ${traceData.length} bytes`);
      console.log('\nView at: https://ui.perfetto.dev');
    } catch (error) {
      console.error('Failed to save trace:', error);
    }
  }
  
  // Clean up hooks
  removeProbe();
  removeFactorialProbe();
  
  console.log('\nExample completed!');
}

// Run the example
main().catch(error => {
  console.error('Error:', error);
  process.exit(1);
});
