/**
 * Test for JSR stack write divergence issue
 *
 * This test reproduces the issue where:
 * - movem.l D0-D7/A0-A6,-(A7) pushes all registers onto stack
 * - jsr $5dc1c.l calls a function
 * - Stack writes should be consistent and complete
 */

const fs = require('fs');
const path = require('path');

// Load the WASM module
async function loadMusashi() {
  const wasmPath = path.join(__dirname, '..', 'musashi-node.out.wasm');
  const wasmBytes = fs.readFileSync(wasmPath);

  const imports = {
    env: {
      memory: new WebAssembly.Memory({ initial: 256 }),
      __linear_memory_base: 0,
      __table_base: 0,
      table: new WebAssembly.Table({ initial: 0, element: 'anyfunc' }),
    }
  };

  const wasmModule = await WebAssembly.instantiate(wasmBytes, imports);
  return wasmModule.instance.exports;
}

// Memory layout for the test
const CALL_ENTRY = 0x0400;
const MOVEM_PC = 0x0400;
const JSR1_PC = 0x0404;
const RETURN_PC = 0x040A;
const TARGET_A = 0x5dc1c;
const STACK_BASE = 0x0010f300;

async function test() {
  console.log('Loading Musashi WASM module...');
  const Module = await loadMusashi();

  console.log('Setting up memory and CPU state...');

  // Initialize CPU
  Module._m68k_init();
  Module._m68k_pulse_reset();

  // Set up initial stack pointer
  Module._m68k_set_reg(15, STACK_BASE); // A7/SP
  Module._m68k_set_reg(0x10, STACK_BASE); // Also set via direct SP access if available

  // Set up test registers
  Module._m68k_set_reg(8, 0x100a80); // A0
  Module._m68k_set_reg(9, 0x100a80); // A1
  Module._m68k_set_reg(0, 0x009c); // D0
  Module._m68k_set_reg(1, 0); // D1
  Module._m68k_set_reg(0x11, 0x2704); // SR

  // Track memory writes
  const memoryWrites = [];
  const stackWrites = [];

  // Set up memory callbacks
  function readMemory(address, size) {
    console.log(`Read: addr=0x${address.toString(16)}, size=${size}`);

    // Handle the test instructions
    if (address >= MOVEM_PC && address < MOVEM_PC + 12) {
      const offset = address - MOVEM_PC;
      const instructions = [
        0x48, 0xe7, 0xff, 0xfe, // movem.l D0-D7/A0-A6, -(A7)
        0x4e, 0xb9, 0x00, 0x05, 0xdc, 0x1c, // jsr $5dc1c.l
        0x4e, 0x75 // rts
      ];
      return instructions[offset] || 0;
    }

    // Handle target function instructions
    if (address >= TARGET_A && address < TARGET_A + 12) {
      const offset = address - TARGET_A;
      const instructions = [
        0x30, 0x3c, 0x00, 0x9c, // move.w #$009c, D0
        0x21, 0xbc, 0xff, 0xff, 0xff, 0xff, // move.l #$ffffffff, (A0,D0.w)
        0x4e, 0x75 // rts
      ];
      return instructions[offset] || 0;
    }

    // Return 0 for other addresses
    return 0;
  }

  function writeMemory(address, size, value) {
    console.log(`Write: addr=0x${address.toString(16)}, size=${size}, value=0x${value.toString(16)}`);
    memoryWrites.push({ address, size, value });

    // Track stack writes specifically
    if (address >= STACK_BASE - 0x100 && address < STACK_BASE) {
      stackWrites.push({ address, size, value });
    }
  }

  // Set memory callbacks (if available)
  if (Module._set_read8_callback) {
    const readCallback = Module.addFunction(readMemory, 'iii');
    Module._set_read8_callback(readCallback);
  }

  if (Module._set_write8_callback) {
    const writeCallback = Module.addFunction(writeMemory, 'viii');
    Module._set_write8_callback(writeCallback);
  }

  // Set up PC hook to track execution
  const pcHooks = [];
  if (Module._set_pc_hook_func) {
    function pcHook(pc) {
      console.log(`PC Hook: 0x${pc.toString(16)}`);
      pcHooks.push(pc);
      return 0; // Continue execution
    }

    const hookCallback = Module.addFunction(pcHook, 'ii');
    Module._set_pc_hook_func(hookCallback);
  }

  console.log('Starting execution...');

  // Set PC to start of test
  Module._m68k_set_reg(0x0F, CALL_ENTRY); // PC

  // Execute the test
  try {
    const cycles = Module._m68k_execute(100); // Execute up to 100 cycles
    console.log(`Executed ${cycles} cycles`);
  } catch (error) {
    console.log('Execution error:', error);
  }

  // Report results
  console.log('\n=== Execution Results ===');
  console.log(`PC hooks fired: ${pcHooks.length}`);
  console.log('PC trace:', pcHooks.map(pc => `0x${pc.toString(16)}`).join(' -> '));

  console.log(`\nTotal memory writes: ${memoryWrites.length}`);
  console.log(`Stack writes: ${stackWrites.length}`);

  console.log('\nStack writes detail:');
  stackWrites.forEach((write, i) => {
    console.log(`  ${i+1}: addr=0x${write.address.toString(16)}, size=${write.size}, value=0x${write.value.toString(16)}`);
  });

  // The bug report mentions "size 6 !== 7" mismatch
  // Let's check if we can detect this pattern
  if (stackWrites.length === 6 || stackWrites.length === 7) {
    console.log(`\n*** POTENTIAL BUG: Stack writes count = ${stackWrites.length} ***`);
    console.log('This matches the reported "size 6 !== 7" fusion mismatch');
  }

  // Final CPU state
  const finalPC = Module._m68k_get_reg(0x0F);
  const finalSP = Module._m68k_get_reg(15);
  console.log(`\nFinal state:`);
  console.log(`  PC: 0x${finalPC.toString(16)}`);
  console.log(`  SP: 0x${finalSP.toString(16)}`);
  console.log(`  SP delta: ${STACK_BASE - finalSP} bytes`);
}

// Run the test
test().catch(console.error);