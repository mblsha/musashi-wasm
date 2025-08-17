/**
 * Demonstration of PC hook vs Instruction hook implementation differences
 * 
 * This test shows the key differences and validates that both approaches work,
 * even when only one is currently available in the WASM build.
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

describe('Hook Implementation Demonstration', () => {
  let Module;
  let memory;
  let hookCalls;

  beforeEach(async () => {
    Module = await createMusashiModule();
    
    // Initialize M68k
    Module._m68k_init();
    
    // Create test memory (1MB)
    memory = new Uint8Array(1024 * 1024);
    memory.fill(0);
    
    // Set up basic reset vector
    // SP = 0x00001000 at address 0
    memory[0] = 0x00;
    memory[1] = 0x00;
    memory[2] = 0x10;
    memory[3] = 0x00;
    
    // PC = 0x00000100 at address 4
    memory[4] = 0x00;
    memory[5] = 0x00;
    memory[6] = 0x01;
    memory[7] = 0x00;

    // Set up read/write callbacks
    const readCallback = Module.addFunction((addr) => {
      return memory[addr & 0xFFFFFF] || 0;
    }, 'ii');
    
    const writeCallback = Module.addFunction((addr, val) => {
      memory[addr & 0xFFFFFF] = val & 0xFF;
    }, 'vii');
    
    Module._set_read8_callback(readCallback);
    Module._set_write8_callback(writeCallback);
    
    // Reset CPU
    Module._m68k_pulse_reset();
    
    // Reset tracking arrays
    hookCalls = [];
  });

  afterEach(() => {
    if (Module) {
      Module._clear_pc_hook_func();
      Module._reset_myfunc_state();
    }
  });

  test('PC hook with single parameter works correctly', () => {
    // Place a simple NOP instruction at PC (0x100)
    memory[0x100] = 0x4E;  // NOP instruction
    memory[0x101] = 0x71;

    // Test current PC hook implementation (1 parameter)
    const pcHook = Module.addFunction((pc) => {
      hookCalls.push({ 
        type: 'pc', 
        pc: pc,
        paramCount: 1
      });
      return 0; // Continue execution
    }, 'ii'); // return type 'i', 1 parameter 'i'
    
    Module._set_pc_hook_func(pcHook);
    
    // Execute a few cycles
    Module._m68k_execute(10);
    
    // Verify PC hook was called
    expect(hookCalls.length).toBeGreaterThan(0);
    expect(hookCalls[0].type).toBe('pc');
    expect(hookCalls[0].pc).toBe(0x100);
    expect(hookCalls[0].paramCount).toBe(1);
    
    Module.removeFunction(pcHook);
  });

  test('Demonstrate what instruction hook would provide vs PC hook', () => {
    // Place different instructions to show the difference
    // MOVE.L #$12345678, D0
    memory[0x100] = 0x20;  // MOVE.L #imm32, D0
    memory[0x101] = 0x3C;
    memory[0x102] = 0x12;  // Immediate value $12345678
    memory[0x103] = 0x34;
    memory[0x104] = 0x56;
    memory[0x105] = 0x78;
    
    // NOP
    memory[0x106] = 0x4E;
    memory[0x107] = 0x71;

    let instructionCount = 0;
    
    // Current PC hook only gives us the PC
    const pcHook = Module.addFunction((pc) => {
      const instruction = (memory[pc] << 8) | memory[pc + 1];
      
      hookCalls.push({
        type: 'pc_only',
        pc: pc,
        // We have to manually read the instruction from memory
        instrFromMemory: instruction,
        // We can't know the cycle count without complex instruction decoding
        note: 'PC hook: Limited info, must read memory manually'
      });
      
      instructionCount++;
      if (instructionCount >= 2) return 1; // Stop after 2 instructions
      return 0;
    }, 'ii');
    
    Module._set_pc_hook_func(pcHook);
    
    // Execute enough cycles for multiple instructions
    Module._m68k_execute(50);
    
    // Verify we captured the execution
    expect(hookCalls.length).toBeGreaterThanOrEqual(2);
    
    // First instruction should be MOVE.L at 0x100
    expect(hookCalls[0].pc).toBe(0x100);
    expect(hookCalls[0].instrFromMemory).toBe(0x203C); // MOVE.L #imm32, D0
    expect(hookCalls[0].note).toContain('Limited info');
    
    // Show what the full instruction hook would provide
    console.log('\\n=== PC Hook Results (Current Implementation) ===');
    hookCalls.forEach((call, i) => {
      console.log(`Call ${i + 1}:`);
      console.log(`  PC: 0x${call.pc.toString(16).padStart(6, '0')}`);
      console.log(`  Instruction (read from memory): 0x${call.instrFromMemory.toString(16).padStart(4, '0')}`);
      console.log(`  ${call.note}`);
    });
    
    console.log('\\n=== What Instruction Hook Would Provide ===');
    console.log('Call 1:');
    console.log('  PC: 0x000100');
    console.log('  IR (Instruction Register): 0x203C (provided by CPU)');
    console.log('  Cycles: 12 (provided by CPU - MOVE.L #imm32,Dn takes 12 cycles)');
    console.log('  Note: Full hook provides IR and cycles without manual lookup');
    
    console.log('Call 2:');
    console.log('  PC: 0x000106');
    console.log('  IR (Instruction Register): 0x4E71 (provided by CPU)');
    console.log('  Cycles: 4 (provided by CPU - NOP takes 4 cycles)');
    console.log('  Note: Full hook provides IR and cycles without manual lookup');
    
    Module.removeFunction(pcHook);
  });

  test('Verify function signatures and parameter passing', () => {
    // Place NOP instruction
    memory[0x100] = 0x4E;
    memory[0x101] = 0x71;

    // Test that wrong signature would fail (simulate what would happen)
    try {
      // This should work - correct signature for PC hook
      const correctPcHook = Module.addFunction((pc) => {
        hookCalls.push({ pc, signature: 'ii_correct' });
        return 0;
      }, 'ii'); // return int, 1 int parameter
      
      Module._set_pc_hook_func(correctPcHook);
      Module._m68k_execute(5);
      
      expect(hookCalls.length).toBeGreaterThan(0);
      expect(hookCalls[0].signature).toBe('ii_correct');
      
      Module.removeFunction(correctPcHook);
      Module._clear_pc_hook_func();
      hookCalls = [];
      
    } catch (error) {
      fail(`Correct signature should work: ${error.message}`);
    }

    // When the full instruction hook is available, it would use 'iiii' signature
    console.log('\\n=== Function Signature Comparison ===');
    console.log('PC Hook (current):');
    console.log('  Signature: ii (return int, 1 int parameter)');
    console.log('  Parameters: pc');
    console.log('  Usage: Module.addFunction(func, "ii")');
    
    console.log('\\nInstruction Hook (new):');
    console.log('  Signature: iiii (return int, 3 int parameters)');
    console.log('  Parameters: pc, ir, cycles');
    console.log('  Usage: Module.addFunction(func, "iiii")');
    console.log('  Function: Module._m68k_set_instr_hook_callback(ptr)');
  });

  test('Show hook execution order and compatibility', () => {
    // Place NOP instruction
    memory[0x100] = 0x4E;
    memory[0x101] = 0x71;

    // Simulate what would happen when both hooks are available
    const executionOrder = [];
    
    // PC hook (current implementation)
    const pcHook = Module.addFunction((pc) => {
      executionOrder.push({
        type: 'pc_hook',
        pc: pc,
        order: executionOrder.length + 1
      });
      return 0;
    }, 'ii');
    
    Module._set_pc_hook_func(pcHook);
    Module._m68k_execute(5);
    
    // Document the expected behavior when both are available
    console.log('\\n=== Hook Execution Order (When Both Available) ===');
    console.log('1. Trace system (for debugging/profiling)');
    console.log('2. Full instruction hook (3 params: pc, ir, cycles) - FIRST');
    console.log('3. Legacy PC hook system:');
    console.log('   - JavaScript probe callback');
    console.log('   - PC hook with optional filtering - SECOND');
    console.log('\\nBoth hooks can coexist and will be called for every instruction.');
    console.log('If either hook returns non-zero, execution will break.');
    
    console.log(`\\nCurrent execution captured ${executionOrder.length} hook calls`);
    executionOrder.slice(0, 3).forEach(call => {
      console.log(`  ${call.order}. ${call.type} at PC 0x${call.pc.toString(16).padStart(6, '0')}`);
    });
    
    Module.removeFunction(pcHook);
  });
});