/**
 * Simple test to analyze JSR and MOVEM stack behavior
 * Using the musashi-node.out.mjs directly
 */

import fs from 'fs';
import { fileURLToPath } from 'url';
import { dirname, resolve } from 'path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

// Load the WASM module
async function loadMusashi() {
  try {
    const moduleFactory = await import('./musashi-node.out.mjs');
    const Module = await moduleFactory.default();
    return Module;
  } catch (error) {
    console.error('Failed to load Musashi WASM module:', error);
    process.exit(1);
  }
}

// Constants from bug report
const CALL_ENTRY = 0x0400;
const MOVEM_PC = 0x0400;
const JSR1_PC = 0x0404;
const TARGET_A = 0x5dc1c;
const STACK_BASE = 0x0010f300;

async function testJSRStackWrites() {
  console.log('Loading Musashi WASM module...');
  const Module = await loadMusashi();

  console.log('Setting up test...');

  // Initialize CPU
  Module._m68k_init();
  Module._m68k_pulse_reset();

  // Track all memory writes
  const allWrites = [];
  const stackWrites = [];
  const pcTrace = [];

  // Set up memory allocation
  const MEMORY_SIZE = 1024 * 1024; // 1MB
  const memPtr = Module._malloc(MEMORY_SIZE);
  if (!memPtr) {
    throw new Error('Failed to allocate memory');
  }

  const memory = Module.HEAPU8.subarray(memPtr, memPtr + MEMORY_SIZE);

  try {
    // Add memory region
    Module._add_region(0, MEMORY_SIZE, memPtr);

    // Set up the test instructions
    // movem.l D0-D7/A0-A6, -(A7) at 0x0400
    const movemInstr = [0x48, 0xe7, 0xff, 0xfe];
    memory.set(movemInstr, MOVEM_PC);

    // jsr $5dc1c.l at 0x0404
    const jsrInstr = [0x4e, 0xb9, 0x00, 0x05, 0xdc, 0x1c];
    memory.set(jsrInstr, JSR1_PC);

    // rts at 0x040A
    memory.set([0x4e, 0x75], 0x040A);

    // Target function at 0x5dc1c
    memory.set([0x30, 0x3c, 0x00, 0x9c], TARGET_A); // move.w #$009c, D0
    memory.set([0x21, 0xbc, 0xff, 0xff, 0xff, 0xff], TARGET_A + 4); // move.l #$ffffffff, (A0,D0.w)
    memory.set([0x4e, 0x75], TARGET_A + 10); // rts

    // Set up write callback
    const writeCallback = Module.addFunction((address, size, value) => {
      const writeInfo = { address, size, value };
      allWrites.push(writeInfo);

      console.log(`Write: addr=0x${address.toString(16)}, size=${size}, value=0x${value.toString(16)}`);

      // Track stack writes
      if (address >= STACK_BASE - 0x200 && address < STACK_BASE) {
        stackWrites.push(writeInfo);
        console.log(`  -> STACK WRITE #${stackWrites.length}`);
      }
    }, 'viii');

    Module._set_write_mem_func(writeCallback);

    // Set up PC hook
    const pcCallback = Module.addFunction((pc) => {
      console.log(`PC: 0x${pc.toString(16)}`);
      pcTrace.push(pc);

      // Stop after reaching target function's RTS
      if (pc === TARGET_A + 10) {
        return 1; // Stop execution
      }
      return 0; // Continue
    }, 'ii');

    Module._set_pc_hook_func(pcCallback);

    // Set initial CPU state as per bug report
    Module._m68k_set_reg(15, STACK_BASE); // A7/SP
    Module._m68k_set_reg(8, 0x100a80);   // A0
    Module._m68k_set_reg(9, 0x100a80);   // A1
    Module._m68k_set_reg(0, 0x009c);     // D0
    Module._m68k_set_reg(1, 0);          // D1
    Module._m68k_set_reg(17, 0x2704);    // SR
    Module._m68k_set_reg(16, CALL_ENTRY); // PC

    console.log('\nInitial state:');
    console.log(`  PC: 0x${Module._m68k_get_reg(0, 16).toString(16)}`);
    console.log(`  SP: 0x${Module._m68k_get_reg(0, 15).toString(16)}`);
    console.log(`  A0: 0x${Module._m68k_get_reg(0, 8).toString(16)}`);
    console.log(`  D0: 0x${Module._m68k_get_reg(0, 0).toString(16)}`);

    // Execute the test
    console.log('\n=== Starting execution ===');
    const cycles = Module._m68k_execute(1000);
    console.log(`Executed ${cycles} cycles`);

    // Analyze results
    console.log('\n=== Results ===');
    console.log(`PC trace: ${pcTrace.map(pc => `0x${pc.toString(16)}`).join(' -> ')}`);
    console.log(`Total writes: ${allWrites.length}`);
    console.log(`Stack writes: ${stackWrites.length}`);

    // Final state
    const finalPC = Module._m68k_get_reg(0, 16);
    const finalSP = Module._m68k_get_reg(0, 15);
    console.log(`\nFinal state:`);
    console.log(`  PC: 0x${finalPC.toString(16)}`);
    console.log(`  SP: 0x${finalSP.toString(16)}`);
    console.log(`  SP delta: ${STACK_BASE - finalSP} bytes`);

    console.log('\nStack write details:');
    stackWrites.forEach((write, i) => {
      console.log(`  ${i+1}: addr=0x${write.address.toString(16)}, size=${write.size}, value=0x${write.value.toString(16)}`);
    });

    // Check for the fusion bug pattern
    if (stackWrites.length === 6 || stackWrites.length === 7) {
      console.log(`\n*** POTENTIAL BUG PATTERN DETECTED ***`);
      console.log(`Stack write count: ${stackWrites.length}`);
      console.log('This matches the "size 6 !== 7" pattern from the bug report!');
    }

    // Create diagnostic data
    const diagnostic = {
      testCase: 'jsr_stack_write_simple',
      backend: 'musashi-wasm-local',
      timestamp: new Date().toISOString(),
      stackWriteCount: stackWrites.length,
      totalWriteCount: allWrites.length,
      stackDelta: STACK_BASE - finalSP,
      expectedStackDelta: 60, // 15 registers * 4 bytes
      stackWrites: stackWrites.map(w => ({
        address: `0x${w.address.toString(16)}`,
        size: w.size,
        value: `0x${w.value.toString(16)}`
      })),
      executionTrace: pcTrace.map(pc => `0x${pc.toString(16)}`)
    };

    // Write diagnostic file
    const diagnosticFile = `test-results/fusion-diagnostic-${Date.now()}-wasm-test.json`;
    await fs.promises.mkdir('test-results', { recursive: true });
    await fs.promises.writeFile(diagnosticFile, JSON.stringify(diagnostic, null, 2));
    console.log(`\nDiagnostic data written to: ${diagnosticFile}`);

    // Cleanup
    Module.removeFunction(writeCallback);
    Module.removeFunction(pcCallback);

  } finally {
    Module._free(memPtr);
    Module._clear_regions();
  }
}

// Run the test
testJSRStackWrites().catch(console.error);