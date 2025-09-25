/**
 * Test to reproduce JSR stack write divergence issue
 *
 * Based on bug report: TP Musashi stack-write divergence repro
 * Tests the specific case where movem.l D0-D7/A0-A6,-(A7) followed by jsr
 * causes inconsistent stack write reporting between backends.
 */

import { createSystem } from './index.js';
import type { System, MemoryAccessEvent } from './types.js';

// Constants from bug report
const CALL_ENTRY = 0x0400;
const MOVEM_PC = 0x0400;
const JSR1_PC = 0x0404;
const RETURN_PC = 0x040A;
const TARGET_A = 0x5dc1c;
const STACK_BASE = 0x0010f300;

describe('JSR Stack Write Divergence Reproduction', () => {
  let system: System;

  beforeAll(async () => {
    // Create a large ROM with our test code
    const rom = new Uint8Array(0x300000); // 3MB ROM to cover TARGET_A

    // Set up the instructions as described in bug report
    // movem.l D0-D7/A0-A6, -(A7) at 0x0400
    const movemInstr = [0x48, 0xe7, 0xff, 0xfe];
    rom.set(movemInstr, MOVEM_PC);

    // jsr $5dc1c.l at 0x0404
    const jsrInstr = [0x4e, 0xb9, 0x00, 0x05, 0xdc, 0x1c];
    rom.set(jsrInstr, JSR1_PC);

    // rts at 0x040A
    const rtsInstr = [0x4e, 0x75];
    rom.set(rtsInstr, RETURN_PC);

    // Target function at 0x5dc1c
    // move.w #$009c, D0
    const moveWInstr = [0x30, 0x3c, 0x00, 0x9c];
    rom.set(moveWInstr, TARGET_A);

    // move.l #$ffffffff, (A0,D0.w)
    const moveLInstr = [0x21, 0xbc, 0xff, 0xff, 0xff, 0xff];
    rom.set(moveLInstr, TARGET_A + 4);

    // rts
    rom.set(rtsInstr, TARGET_A + 10);

    system = await createSystem({
      rom,
      ramSize: 0x100000, // 1MB RAM as mentioned in bug report
      memoryLayout: [
        { base: 0x000000, size: 0x300000, type: 'ROM' },
        { base: 0x100000, size: 0x100000, type: 'RAM' },
      ],
    });
  });

  afterAll(() => {
    system.cleanup();
  });

  it('should track stack writes during movem and jsr sequence', () => {
    const stackWrites: MemoryAccessEvent[] = [];
    const allWrites: MemoryAccessEvent[] = [];
    const pcTrace: number[] = [];

    // Set up memory write tracking
    const unsubscribeWrite = system.onMemoryWrite((event) => {
      console.log(`Write: addr=0x${event.addr.toString(16)}, size=${event.size}, value=0x${event.value.toString(16)}`);
      allWrites.push(event);

      // Track stack writes specifically
      if (event.addr >= STACK_BASE - 0x200 && event.addr < STACK_BASE) {
        stackWrites.push(event);
        console.log(`STACK WRITE #${stackWrites.length}: addr=0x${event.addr.toString(16)}, size=${event.size}, value=0x${event.value.toString(16)}`);
      }
    });

    // Set up PC tracking to trace execution flow
    const unsubscribeProbe = system.probe(TARGET_A, () => {
      pcTrace.push(TARGET_A);
    });

    try {
      // Set up CPU state as described in bug report
      system.setRegister('sp', STACK_BASE);
      system.setRegister('a0', 0x100a80);
      system.setRegister('a1', 0x100a80);
      system.setRegister('d0', 0x009c);
      system.setRegister('d1', 0);
      system.setRegister('sr', 0x2704);

      console.log('Initial state:');
      const initialRegs = system.getRegisters();
      console.log(`  PC: 0x${initialRegs.pc.toString(16)}`);
      console.log(`  SP: 0x${initialRegs.sp.toString(16)}`);
      console.log(`  A0: 0x${initialRegs.a0.toString(16)}`);
      console.log(`  D0: 0x${initialRegs.d0.toString(16)}`);

      // Execute the test sequence
      console.log('\n=== Starting execution ===');
      const result = system.call(CALL_ENTRY);
      console.log(`Execution completed with result: ${result}`);

      // Report results
      console.log('\n=== Execution Results ===');
      console.log(`PC trace hits at target: ${pcTrace.length}`);
      console.log(`Total writes: ${allWrites.length}`);
      console.log(`Stack writes: ${stackWrites.length}`);

      console.log('\nStack write details:');
      stackWrites.forEach((write, i) => {
        console.log(`  ${i+1}: addr=0x${write.addr.toString(16)}, size=${write.size}, value=0x${write.value.toString(16)}`);
      });

      // Final CPU state
      const finalRegs = system.getRegisters();
      console.log(`\nFinal state:`);
      console.log(`  PC: 0x${finalRegs.pc.toString(16)}`);
      console.log(`  SP: 0x${finalRegs.sp.toString(16)}`);
      console.log(`  SP delta: ${STACK_BASE - finalRegs.sp} bytes`);

      // Check for the bug pattern mentioned in the report
      if (stackWrites.length === 6 || stackWrites.length === 7) {
        console.log(`\n*** BUG REPRODUCTION: Stack writes count = ${stackWrites.length} ***`);
        console.log('This matches the reported "size 6 !== 7" fusion mismatch!');
      }

      // Basic assertions
      expect(pcTrace.length).toBeGreaterThan(0);
      expect(stackWrites.length).toBeGreaterThan(0);

      // The movem.l D0-D7/A0-A6,-(A7) should push 15 registers * 4 bytes = 60 bytes
      // But the bug report suggests inconsistent reporting
      console.log(`\nExpected stack usage: 60 bytes (15 registers * 4 bytes)`);
      console.log(`Actual SP delta: ${STACK_BASE - finalRegs.sp} bytes`);

      // Check if we reached the target function
      expect(pcTrace.length).toBeGreaterThan(0);

      // Document the behavior for further analysis
      console.log('\n=== Analysis for Bug Report ===');
      console.log(`This test documents the current behavior of the musashi-wasm core.`);
      console.log(`Stack write count: ${stackWrites.length}`);
      console.log(`This data should be compared with TP backend results.`);

      // Create a diagnostic object similar to what the bug report mentions
      const diagnostic = {
        testCase: 'jsr_stack_write_repro',
        backend: 'musashi-wasm',
        stackWriteCount: stackWrites.length,
        totalWriteCount: allWrites.length,
        stackDelta: STACK_BASE - finalRegs.sp,
        stackWrites: stackWrites.map(w => ({
          address: `0x${w.addr.toString(16)}`,
          size: w.size,
          value: `0x${w.value.toString(16)}`
        })),
        executionTrace: pcTrace.map(pc => `0x${pc.toString(16)}`)
      };

      console.log('\n=== Diagnostic Data ===');
      console.log(JSON.stringify(diagnostic, null, 2));

    } finally {
      unsubscribeWrite();
      unsubscribeProbe();
    }
  });

  it('should provide detailed analysis of movem instruction execution', () => {
    const writeSequence: (MemoryAccessEvent & { order: number })[] = [];

    // Track writes with order
    const unsubscribeWrite = system.onMemoryWrite((event) => {
      const writeInfo = {
        ...event,
        order: writeSequence.length
      };
      writeSequence.push(writeInfo);
      console.log(`Write ${writeInfo.order}: addr=0x${event.addr.toString(16)}, size=${event.size}, value=0x${event.value.toString(16)}`);
    });

    try {
      // Set up initial state for just the movem instruction
      system.reset();
      system.setRegister('sp', STACK_BASE);

      // Use step() to execute just the movem instruction
      console.log('\n=== MOVEM Instruction Analysis ===');

      // Set PC to the movem instruction
      system.setRegister('pc', MOVEM_PC);

      const stepResult = system.step();
      console.log(`Step result: ${JSON.stringify(stepResult)}`);

      // Filter writes that happened during this step
      console.log(`\nTotal writes during movem step: ${writeSequence.length}`);

      // Check for write size patterns
      const writeSizes = writeSequence.map(w => w.size);
      const sizeDistribution = writeSizes.reduce((acc, size) => {
        acc[size] = (acc[size] || 0) + 1;
        return acc;
      }, {} as Record<number, number>);

      console.log('Write size distribution:', sizeDistribution);

      // This gives us baseline data for comparison with TP backend
      console.log('\n=== Data for Fusion Comparison ===');
      console.log('WASM Backend Results:');
      console.log(`- Write count: ${writeSequence.length}`);
      console.log(`- Size distribution: ${JSON.stringify(sizeDistribution)}`);
      if (writeSequence.length > 0) {
        console.log(`- Address range: 0x${Math.min(...writeSequence.map(w => w.addr)).toString(16)} - 0x${Math.max(...writeSequence.map(w => w.addr)).toString(16)}`);
      }

    } finally {
      unsubscribeWrite();
    }
  });
});