/**
 * Simple test to debug CPU execution and hook calling
 */

import { test, expect } from '@jest/globals';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Path to the WASM module, assuming it's in the parent directory
const modulePath = path.resolve(__dirname, '../../musashi-node.out.mjs');

// Pre-flight check to ensure the module exists before running tests.
if (!fs.existsSync(modulePath)) {
    console.error(`ERROR: WASM module not found at ${modulePath}`);
    process.exit(1);
}

// Load the factory function for the WASM module
import createMusashiModule from '../load-musashi.js';

describe('CPU Execution Debug Tests', () => {
  let Module;

  beforeEach(async () => {
    Module = await createMusashiModule();
    
    // Enable debug logging
    Module._enable_printf_logging();
    
    // Initialize M68k
    Module._m68k_init();
  });

  test('CPU execution basic verification', () => {
    console.log('\n=== CPU Execution Basic Test ===');
    
    const MEMORY_SIZE = 64 * 1024; // 64KB
    const PROG_START_ADDR = 0x400;
    const STACK_POINTER_ADDR = 0x8000;

    // Allocate memory inside the WASM module heap
    const wasmMemoryPtr = Module._malloc(MEMORY_SIZE);
    console.log(`Allocated WASM memory at ptr: 0x${wasmMemoryPtr.toString(16)} size: 0x${MEMORY_SIZE.toString(16)}`);
    
    // Get a view of the WASM memory to manipulate it from JS
    const jsMemory = Module.HEAPU8.subarray(wasmMemoryPtr, wasmMemoryPtr + MEMORY_SIZE);

    try {
      // Map this WASM memory as a region for fast access
      console.log(`Adding region: start=0x0 size=0x${MEMORY_SIZE.toString(16)} ptr=0x${wasmMemoryPtr.toString(16)}`);
      Module.ccall('add_region', 'void', ['number', 'number', 'number'], [0x0, MEMORY_SIZE, wasmMemoryPtr]);

      // Place a simple NOP instruction at the program start
      jsMemory[PROG_START_ADDR] = 0x4E;  // NOP instruction
      jsMemory[PROG_START_ADDR + 1] = 0x71;

      // Set reset vectors (big-endian format)
      const spBytes = new Uint8Array(new Uint32Array([STACK_POINTER_ADDR]).buffer).reverse();
      const pcBytes = new Uint8Array(new Uint32Array([PROG_START_ADDR]).buffer).reverse();
      console.log(`Setting reset vectors: SP=0x${STACK_POINTER_ADDR.toString(16)} at addr 0, PC=0x${PROG_START_ADDR.toString(16)} at addr 4`);
      console.log(`SP bytes:`, Array.from(spBytes).map(b => b.toString(16).padStart(2, '0')).join(' '));
      console.log(`PC bytes:`, Array.from(pcBytes).map(b => b.toString(16).padStart(2, '0')).join(' '));
      jsMemory.set(spBytes, 0);
      jsMemory.set(pcBytes, 4);
      
      // Verify what was written
      console.log(`Memory at 0-7:`, Array.from(jsMemory.slice(0, 8)).map(b => b.toString(16).padStart(2, '0')).join(' '));

      // Set up PC hook (using correct API like working test)
      const pcHookCalls = [];
      const pcHookPtr = Module.addFunction((pc) => {
        console.log(`PC Hook called with pc=0x${pc.toString(16)}`);
        pcHookCalls.push(pc);
        return 0; // Continue execution
      }, 'ii');
      
      Module.ccall('set_pc_hook_func', 'void', ['number'], [pcHookPtr]);
      console.log('PC hook function set via ccall');

      // Reset CPU
      console.log('Calling m68k_pulse_reset...');
      Module._m68k_pulse_reset();

      // Check PC after reset
      const pc_after_reset = Module._m68k_get_reg(16); // M68K_REG_PC = 16
      console.log(`PC after reset: 0x${pc_after_reset.toString(16)}`);

      // Execute instructions
      console.log('Executing instructions...');
      const cycles = Module._m68k_execute(10);
      console.log(`Executed ${cycles} cycles`);

      // Check results
      console.log(`PC hook was called ${pcHookCalls.length} times`);
      console.log(`PC hook calls: ${pcHookCalls.map(pc => '0x' + pc.toString(16)).join(', ')}`);

      // Clean up
      Module.removeFunction(pcHookPtr);
      
      // Verify hook was called
      expect(pcHookCalls.length).toBeGreaterThan(0);
      expect(pcHookCalls[0]).toBe(PROG_START_ADDR);
      
    } finally {
      // Clean up WASM heap
      Module._free(wasmMemoryPtr);
      Module.ccall('clear_regions', 'void', [], []);
    }
  });

  test('Compare with working test approach', () => {
    console.log('\n=== Compare With Working Test Approach ===');
    
    // This is essentially a copy of the working test setup
    const machineCode = new Uint8Array([
      0x4E, 0x71  // NOP
    ]);

    const MEMORY_SIZE = 64 * 1024; // 64KB
    const PROG_START_ADDR = 0x400;
    const STACK_POINTER_ADDR = 0x8000;

    let jsMemory, wasmMemoryPtr, pcHookPtr;

    try {
      // Allocate memory inside the WASM module heap for our main RAM
      wasmMemoryPtr = Module._malloc(MEMORY_SIZE);
      console.log(`Allocated WASM memory at ptr: 0x${wasmMemoryPtr.toString(16)} size: 0x${MEMORY_SIZE.toString(16)}`);
      expect(wasmMemoryPtr).not.toBe(0);

      // Get a view of the WASM memory to manipulate it from JS
      jsMemory = Module.HEAPU8.subarray(wasmMemoryPtr, wasmMemoryPtr + MEMORY_SIZE);

      // Map this WASM memory as a region for fast access
      console.log(`Adding region: start=0x0 size=0x${MEMORY_SIZE.toString(16)} ptr=0x${wasmMemoryPtr.toString(16)}`);
      Module.ccall('add_region', 'void', ['number', 'number', 'number'], [0x0, MEMORY_SIZE, wasmMemoryPtr]);

      // Set up PC hook exactly like working test
      const pcLog = [];
      const pcHookSpy = (pc) => {
        console.log(`PC Hook called with pc=0x${pc.toString(16)}`);
        pcLog.push(pc);
        return 0; // Continue execution
      };
      pcHookPtr = Module.addFunction(pcHookSpy, 'ii');
      Module.ccall('set_pc_hook_func', 'void', ['number'], [pcHookPtr]);

      // Write program to memory
      jsMemory.set(machineCode, PROG_START_ADDR);

      // Set reset vectors (big-endian format) - exactly like working test
      const spBytes = new Uint8Array(new Uint32Array([STACK_POINTER_ADDR]).buffer).reverse();
      const pcBytes = new Uint8Array(new Uint32Array([PROG_START_ADDR]).buffer).reverse();
      console.log(`Setting reset vectors: SP=0x${STACK_POINTER_ADDR.toString(16)} at addr 0, PC=0x${PROG_START_ADDR.toString(16)} at addr 4`);
      jsMemory.set(spBytes, 0);
      jsMemory.set(pcBytes, 4);
      
      // Verify what was written
      console.log(`Memory at 0-7:`, Array.from(jsMemory.slice(0, 8)).map(b => b.toString(16).padStart(2, '0')).join(' '));

      // Reset CPU exactly like working test
      console.log('Calling m68k_pulse_reset...');
      Module._m68k_pulse_reset();

      // Execute the program exactly like working test
      const cycles = Module._m68k_execute(10);
      console.log(`Executed ${cycles} cycles`);
      expect(cycles).toBeGreaterThan(0);

      console.log(`PC hook was called ${pcLog.length} times`);
      console.log(`PC hook calls: ${pcLog.map(pc => '0x' + pc.toString(16)).join(', ')}`);

      // Verify PC hook was called
      expect(pcLog.length).toBeGreaterThan(0);
      expect(pcLog[0]).toBe(PROG_START_ADDR);

    } finally {
      // Clean up WASM heap and function table
      if (wasmMemoryPtr) Module._free(wasmMemoryPtr);
      if (pcHookPtr) Module.removeFunction(pcHookPtr);
      Module.ccall('clear_regions', 'void', [], []);
    }
  });
});