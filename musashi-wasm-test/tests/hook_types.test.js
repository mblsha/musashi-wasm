/**
 * Test both PC hook (1 param) and Instruction hook (3 params) functionality
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
    console.error(`\n============================================`);
    console.error(`ERROR: WASM module not found at ${modulePath}`);
    console.error(`============================================\n`);
    console.error(`The Musashi WASM module has not been built yet.\n`);
    console.error(`To build the WASM module, you need:`);
    console.error(`1. Emscripten SDK installed and configured`);
    console.error(`2. Run one of the following commands from the Musashi root directory:`);
    console.error(`   - fish build.fish (if Fish shell is available)`);
    console.error(`   - emmake make -j8 && emcc [options] (manual build)`);
    console.error(`\nFor detailed instructions, see the README.md file.`);
    console.error(`\n============================================\n`);
    process.exit(1);
}

// Load the factory function for the WASM module
import createMusashiModule from '../load-musashi.js';

describe('Hook Types Comprehensive Tests', () => {
  let Module;
  let memory;
  let pcHookCalls;
  let instrHookCalls;

  beforeEach(async () => {
    Module = await createMusashiModule();
    
    // Enable debug logging
    Module._enable_printf_logging();
    
    // Initialize M68k
    Module._m68k_init();
    
    // Clear previous state
    Module.ccall('clear_regions', 'void', [], []);
    Module.ccall('set_read_mem_func', 'void', ['number'], [0]);
    Module.ccall('set_write_mem_func', 'void', ['number'], [0]);
    Module.ccall('clear_pc_hook_func', 'void', [], []);
    
    // Set up memory using WASM region approach (like working test)
    const MEMORY_SIZE = 64 * 1024; // 64KB
    const wasmMemoryPtr = Module._malloc(MEMORY_SIZE);
    const jsMemory = Module.HEAPU8.subarray(wasmMemoryPtr, wasmMemoryPtr + MEMORY_SIZE);
    
    // Map this WASM memory as a region for fast access
    Module.ccall('add_region', 'void', ['number', 'number', 'number'], [0x0, MEMORY_SIZE, wasmMemoryPtr]);
    
    // Set up reset vectors (using working test addresses)
    const STACK_POINTER_ADDR = 0x8000;
    const PROG_START_ADDR = 0x400;
    
    const spBytes = new Uint8Array(new Uint32Array([STACK_POINTER_ADDR]).buffer).reverse();
    const pcBytes = new Uint8Array(new Uint32Array([PROG_START_ADDR]).buffer).reverse();
    jsMemory.set(spBytes, 0);
    jsMemory.set(pcBytes, 4);
    
    // Use working test's instruction sequence: MOVE.L #$DEADBEEF, D0; NOP
    const machineCode = new Uint8Array([
        0x20, 0x3C, 0xDE, 0xAD, 0xBE, 0xEF, // MOVE.L #$DEADBEEF, D0 (6 bytes)
        0x4E, 0x71                          // NOP (2 bytes)
    ]);
    jsMemory.set(machineCode, PROG_START_ADDR);
    
    // Set up write callback for memory outside the region (like working test)
    const writeCallbackSpy = (address, size, value) => {
      console.log(`Write callback: addr=0x${address.toString(16)} size=${size} value=0x${value.toString(16)}`);
    };
    const writeCallbackPtr = Module.addFunction(writeCallbackSpy, 'viii');
    Module.ccall('set_write_mem_func', 'void', ['number'], [writeCallbackPtr]);
    
    // CRITICAL: Add read callback too (like working test)
    const readCallbackSpy = (address, size) => {
      console.log(`Read callback: addr=0x${address.toString(16)} size=${size}`);
      return 0; // Return dummy value
    };
    const readCallbackPtr = Module.addFunction(readCallbackSpy, 'iii');
    Module.ccall('set_read_mem_func', 'void', ['number'], [readCallbackPtr]);
    
    // Store references for cleanup
    Module._testCleanup = {
      wasmMemoryPtr,
      writeCallbackPtr,
      readCallbackPtr
    };
    
    // Reset CPU
    Module._m68k_pulse_reset();
    
    // Reset tracking arrays
    pcHookCalls = [];
    instrHookCalls = [];
  });

  afterEach(() => {
    if (Module && Module._testCleanup) {
      Module._clear_pc_hook_func();
      Module._clear_instr_hook_func();
      if (Module._testCleanup.wasmMemoryPtr) Module._free(Module._testCleanup.wasmMemoryPtr);
      if (Module._testCleanup.writeCallbackPtr) Module.removeFunction(Module._testCleanup.writeCallbackPtr);
      if (Module._testCleanup.readCallbackPtr) Module.removeFunction(Module._testCleanup.readCallbackPtr);
      Module.ccall('clear_regions', 'void', [], []);
      Module._reset_myfunc_state();
    }
  });

  test('PC hook receives only PC parameter (1 param)', () => {
    // Set up PC hook (1 parameter: pc)
    const pcHook = Module.addFunction((pc) => {
      console.log(`PC Hook called with pc=0x${pc.toString(16)}`);
      pcHookCalls.push({ pc });
      return 0; // Continue execution
    }, 'ii'); // return type 'i', 1 parameter 'i'
    
    Module.ccall('set_pc_hook_func', 'void', ['number'], [pcHook]);
    console.log('PC hook function set via ccall');
    
    // Check PC after reset (need context=0 as first parameter)
    const pc_after_reset = Module._m68k_get_reg(0, 16); // M68K_REG_PC = 16
    console.log(`PC after reset: 0x${pc_after_reset.toString(16)}`);
    
    // Execute a few cycles
    console.log('About to execute instructions...');
    const cycles_executed = Module._m68k_execute(50); // More cycles for complex instruction
    console.log(`Executed ${cycles_executed} cycles`);
    
    // Check PC after execution
    const pc_after_execution = Module._m68k_get_reg(0, 16);
    console.log(`PC after execution: 0x${pc_after_execution.toString(16)}`);
    
    // Verify PC hook was called with correct signature
    expect(pcHookCalls.length).toBeGreaterThan(0);
    expect(pcHookCalls[0]).toHaveProperty('pc');
    expect(pcHookCalls[0].pc).toBe(0x400);
    
    // Ensure only PC was captured (no ir or cycles)
    expect(Object.keys(pcHookCalls[0])).toEqual(['pc']);
    
    Module.removeFunction(pcHook);
  });

  test('Instruction hook receives PC, IR, and cycles (3 params)', () => {
    // Set up instruction hook (3 parameters: pc, ir, cycles)
    const instrHook = Module.addFunction((pc, ir, cycles) => {
      console.log(`Instruction Hook called with pc=0x${pc.toString(16)} ir=0x${ir.toString(16)} cycles=${cycles}`);
      instrHookCalls.push({ pc, ir, cycles });
      return 0; // Continue execution
    }, 'iiii'); // return type 'i', 3 parameters 'iii'
    
    Module._set_full_instr_hook_func(instrHook);
    
    // Execute a few cycles
    Module._m68k_execute(50);
    
    // Verify instruction hook was called with correct signature
    expect(instrHookCalls.length).toBeGreaterThan(0);
    expect(instrHookCalls[0]).toHaveProperty('pc');
    expect(instrHookCalls[0]).toHaveProperty('ir');
    expect(instrHookCalls[0]).toHaveProperty('cycles');
    
    // Verify values make sense
    expect(instrHookCalls[0].pc).toBe(0x400);
    expect(instrHookCalls[0].ir).toBe(0x203C); // MOVE.L #imm32, D0 instruction
    expect(instrHookCalls[0].cycles).toBeGreaterThan(0);
    
    Module.removeFunction(instrHook);
  });

  test('Both hooks can coexist without conflicts', () => {
    // Set up both hooks
    const pcHook = Module.addFunction((pc) => {
      pcHookCalls.push({ pc });
      return 0;
    }, 'ii');
    
    const instrHook = Module.addFunction((pc, ir, cycles) => {
      instrHookCalls.push({ pc, ir, cycles });
      return 0;
    }, 'iiii');
    
    Module.ccall('set_pc_hook_func', 'void', ['number'], [pcHook]);
    Module._set_full_instr_hook_func(instrHook);
    
    // Execute a few cycles
    Module._m68k_execute(50);
    
    // Both hooks should have been called
    expect(pcHookCalls.length).toBeGreaterThan(0);
    expect(instrHookCalls.length).toBeGreaterThan(0);
    
    // Verify they both captured the same PC
    expect(pcHookCalls[0].pc).toBe(0x400);
    expect(instrHookCalls[0].pc).toBe(0x400);
    
    // But only instruction hook has ir and cycles
    expect(pcHookCalls[0]).not.toHaveProperty('ir');
    expect(pcHookCalls[0]).not.toHaveProperty('cycles');
    expect(instrHookCalls[0]).toHaveProperty('ir');
    expect(instrHookCalls[0]).toHaveProperty('cycles');
    
    Module.removeFunction(pcHook);
    Module.removeFunction(instrHook);
  });

  test('Instruction hook receives correct IR values for different instructions', () => {
    // Access the memory region set up in beforeEach 
    const jsMemory = Module.HEAPU8.subarray(Module._testCleanup.wasmMemoryPtr, Module._testCleanup.wasmMemoryPtr + 64 * 1024);
    
    // Place different instructions in memory
    // MOVE.L #$12345678, D0
    jsMemory[0x400] = 0x20;  // MOVE.L #imm32, D0
    jsMemory[0x401] = 0x3C;
    jsMemory[0x402] = 0x12;  // Immediate value $12345678
    jsMemory[0x403] = 0x34;
    jsMemory[0x404] = 0x56;
    jsMemory[0x405] = 0x78;
    
    // NOP
    jsMemory[0x406] = 0x4E;
    jsMemory[0x407] = 0x71;

    const instrHook = Module.addFunction((pc, ir, cycles) => {
      instrHookCalls.push({ pc, ir, cycles });
      return 0;
    }, 'iiii');
    
    Module._set_full_instr_hook_func(instrHook);
    
    // Execute enough cycles to process both instructions
    Module._m68k_execute(100); // More cycles to ensure both instructions execute
    
    // Should have at least 1 instruction call (we may only get the MOVE.L)
    expect(instrHookCalls.length).toBeGreaterThanOrEqual(1);
    
    // First instruction should be MOVE.L
    expect(instrHookCalls[0].pc).toBe(0x400);
    expect(instrHookCalls[0].ir).toBe(0x203C); // MOVE.L #imm32, D0
    
    // Find the NOP instruction in the calls
    const nopCall = instrHookCalls.find(call => call.pc === 0x406);
    expect(nopCall).toBeDefined();
    expect(nopCall.ir).toBe(0x4E71); // NOP
    
    Module.removeFunction(instrHook);
  });

  test('Hook return values control execution flow', () => {
    // Access the memory region set up in beforeEach 
    const jsMemory = Module.HEAPU8.subarray(Module._testCleanup.wasmMemoryPtr, Module._testCleanup.wasmMemoryPtr + 64 * 1024);
    
    // Place a sequence of NOP instructions
    for (let i = 0; i < 10; i++) {
      jsMemory[0x400 + i * 2] = 0x4E;  // NOP
      jsMemory[0x400 + i * 2 + 1] = 0x71;
    }

    let callCount = 0;
    const breakingInstrHook = Module.addFunction((pc, ir, cycles) => {
      callCount++;
      instrHookCalls.push({ pc, ir, cycles });
      
      // Break execution after 3 calls
      if (callCount >= 3) {
        return 1; // Non-zero to break
      }
      return 0; // Continue
    }, 'iiii');
    
    Module._set_full_instr_hook_func(breakingInstrHook);
    
    // Execute many cycles, but hook should break early
    Module._m68k_execute(100);
    
    // Should have exactly 3 calls due to early break
    expect(instrHookCalls.length).toBe(3);
    expect(callCount).toBe(3);
    
    Module.removeFunction(breakingInstrHook);
  });

  test('Clear functions properly remove hooks', () => {
    // Set up hooks
    const pcHook = Module.addFunction((pc) => {
      pcHookCalls.push({ pc });
      return 0;
    }, 'ii');
    
    const instrHook = Module.addFunction((pc, ir, cycles) => {
      instrHookCalls.push({ pc, ir, cycles });
      return 0;
    }, 'iiii');
    
    Module.ccall('set_pc_hook_func', 'void', ['number'], [pcHook]);
    Module._set_full_instr_hook_func(instrHook);
    
    // Execute and verify hooks are called
    Module._m68k_execute(50);
    const initialPcCalls = pcHookCalls.length;
    const initialInstrCalls = instrHookCalls.length;
    
    expect(initialPcCalls).toBeGreaterThan(0);
    expect(initialInstrCalls).toBeGreaterThan(0);
    
    // Clear hooks
    Module._clear_pc_hook_func();
    Module._clear_instr_hook_func();
    
    // Reset CPU to initial state
    Module._m68k_pulse_reset();
    
    // Execute again - no new hook calls should occur
    Module._m68k_execute(50);
    
    // Hook call counts should be unchanged
    expect(pcHookCalls.length).toBe(initialPcCalls);
    expect(instrHookCalls.length).toBe(initialInstrCalls);
    
    Module.removeFunction(pcHook);
    Module.removeFunction(instrHook);
  });

  test('Instruction hook provides meaningful cycle information', () => {
    // Access the memory region set up in beforeEach 
    const jsMemory = Module.HEAPU8.subarray(Module._testCleanup.wasmMemoryPtr, Module._testCleanup.wasmMemoryPtr + 64 * 1024);
    
    // Place different instructions with varying cycle counts
    // NOP (4 cycles on 68000)
    jsMemory[0x400] = 0x4E;
    jsMemory[0x401] = 0x71;
    
    // MOVE.W D0,D1 (4 cycles on 68000)
    jsMemory[0x402] = 0x32;
    jsMemory[0x403] = 0x00;

    const instrHook = Module.addFunction((pc, ir, cycles) => {
      instrHookCalls.push({ pc, ir, cycles });
      return 0;
    }, 'iiii');
    
    Module._set_full_instr_hook_func(instrHook);
    
    // Execute enough cycles for both instructions
    Module._m68k_execute(50);
    
    // Should have at least 1 call
    expect(instrHookCalls.length).toBeGreaterThanOrEqual(1);
    
    // All cycle values should be positive
    instrHookCalls.forEach(call => {
      expect(call.cycles).toBeGreaterThan(0);
      expect(call.cycles).toBeLessThan(1000); // Sanity check
    });
    
    Module.removeFunction(instrHook);
  });
});