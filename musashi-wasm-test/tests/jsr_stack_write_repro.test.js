/**
 * Test to reproduce JSR stack write divergence issue
 *
 * Based on bug report: TP Musashi stack-write divergence repro
 * Tests the specific case where movem.l D0-D7/A0-A6,-(A7) followed by jsr
 * causes inconsistent stack write reporting between backends.
 */

import path from 'path';
import fs from 'fs';
import { jest } from '@jest/globals';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const modulePath = path.resolve(__dirname, '../../musashi-node.out.mjs');

if (!fs.existsSync(modulePath)) {
    console.error(`ERROR: WASM module not found at ${modulePath}`);
    process.exit(1);
}

import createMusashiModule from '../load-musashi.js';

// Constants from bug report
const CALL_ENTRY = 0x0400;
const MOVEM_PC = 0x0400;
const JSR1_PC = 0x0404;
const RETURN_PC = 0x040A;
const TARGET_A = 0x5dc1c;
const STACK_BASE = 0x0010f300;

// Register constants
const M68K_REG_D0 = 0;
const M68K_REG_D1 = 1;
const M68K_REG_A0 = 8;
const M68K_REG_A1 = 9;
const M68K_REG_A7 = 15; // SP
const M68K_REG_PC = 16;
const M68K_REG_SR = 17;

describe('JSR Stack Write Divergence Reproduction', () => {
    let Module;

    beforeAll(async () => {
        Module = await createMusashiModule();
        expect(Module).toBeDefined();
        expect(typeof Module._m68k_init).toBe('function');
    });

    beforeEach(() => {
        console.log('\n=== JSR Stack Write Test Setup ===');
        Module._m68k_init();
        Module.ccall('clear_regions', 'void', [], []);
        Module.ccall('set_read_mem_func', 'void', ['number'], [0]);
        Module.ccall('set_write_mem_func', 'void', ['number'], [0]);
        Module.ccall('clear_pc_hook_func', 'void', [], []);
        Module.ccall('enable_printf_logging', 'void', [], []);
    });

    it('should track stack writes during movem and jsr sequence', () => {
        const MEMORY_SIZE = 1024 * 1024; // 1MB as mentioned in bug report

        let wasmMemoryPtr, readCallbackPtr, writeCallbackPtr, pcHookPtr;
        const stackWrites = [];
        const pcTrace = [];
        const allWrites = [];

        try {
            // Allocate WASM memory
            wasmMemoryPtr = Module._malloc(MEMORY_SIZE);
            const jsMemory = Module.HEAPU8.subarray(wasmMemoryPtr, wasmMemoryPtr + MEMORY_SIZE);

            // Map memory regions
            Module.ccall('add_region', 'void', ['number', 'number', 'number'],
                [0x0, MEMORY_SIZE, wasmMemoryPtr]);

            // Set up the instructions as described in bug report
            // movem.l D0-D7/A0-A6, -(A7) at 0x0400
            const movemInstr = [0x48, 0xe7, 0xff, 0xfe];
            jsMemory.set(movemInstr, MOVEM_PC);

            // jsr $5dc1c.l at 0x0404
            const jsrInstr = [0x4e, 0xb9, 0x00, 0x05, 0xdc, 0x1c];
            jsMemory.set(jsrInstr, JSR1_PC);

            // rts at 0x040A
            const rtsInstr = [0x4e, 0x75];
            jsMemory.set(rtsInstr, RETURN_PC);

            // Target function at 0x5dc1c
            // move.w #$009c, D0
            const moveWInstr = [0x30, 0x3c, 0x00, 0x9c];
            jsMemory.set(moveWInstr, TARGET_A);

            // move.l #$ffffffff, (A0,D0.w)
            const moveLInstr = [0x21, 0xbc, 0xff, 0xff, 0xff, 0xff];
            jsMemory.set(moveLInstr, TARGET_A + 4);

            // rts
            jsMemory.set(rtsInstr, TARGET_A + 10);

            // Set up read callback (for debugging)
            readCallbackPtr = Module.addFunction((address, size) => {
                const offset = address - wasmMemoryPtr;
                if (offset >= 0 && offset < MEMORY_SIZE) {
                    if (size === 1) return jsMemory[offset];
                    if (size === 2) return (jsMemory[offset] << 8) | jsMemory[offset + 1];
                    if (size === 4) return (jsMemory[offset] << 24) | (jsMemory[offset + 1] << 16) |
                                          (jsMemory[offset + 2] << 8) | jsMemory[offset + 3];
                }
                return 0;
            }, 'iii');
            Module.ccall('set_read_mem_func', 'void', ['number'], [readCallbackPtr]);

            // Set up write callback to track all writes
            writeCallbackPtr = Module.addFunction((address, size, value) => {
                console.log(`Write: addr=0x${address.toString(16)}, size=${size}, value=0x${value.toString(16)}`);
                allWrites.push({ address, size, value });

                // Track stack writes specifically
                if (address >= STACK_BASE - 0x200 && address < STACK_BASE) {
                    stackWrites.push({ address, size, value });
                    console.log(`STACK WRITE #${stackWrites.length}: addr=0x${address.toString(16)}, size=${size}, value=0x${value.toString(16)}`);
                }
            }, 'viii');
            Module.ccall('set_write_mem_func', 'void', ['number'], [writeCallbackPtr]);

            // Set up PC hook to track execution flow
            pcHookPtr = Module.addFunction((pc) => {
                console.log(`PC: 0x${pc.toString(16)}`);
                pcTrace.push(pc);

                // Stop execution after hitting the target function to limit scope
                if (pc === TARGET_A + 10) { // rts in target function
                    return 1; // Stop execution
                }
                return 0; // Continue
            }, 'ii');
            Module.ccall('set_pc_hook_func', 'void', ['number'], [pcHookPtr]);

            // Set up CPU state as described in bug report
            Module._m68k_set_reg(M68K_REG_A7, STACK_BASE);  // SP
            Module._m68k_set_reg(M68K_REG_A0, 0x100a80);    // A0
            Module._m68k_set_reg(M68K_REG_A1, 0x100a80);    // A1
            Module._m68k_set_reg(M68K_REG_D0, 0x009c);      // D0
            Module._m68k_set_reg(M68K_REG_D1, 0);           // D1
            Module._m68k_set_reg(M68K_REG_SR, 0x2704);      // SR
            Module._m68k_set_reg(M68K_REG_PC, CALL_ENTRY);  // PC

            console.log(`Initial state:`);
            console.log(`  PC: 0x${Module._m68k_get_reg(0, M68K_REG_PC).toString(16)}`);
            console.log(`  SP: 0x${Module._m68k_get_reg(0, M68K_REG_A7).toString(16)}`);
            console.log(`  A0: 0x${Module._m68k_get_reg(0, M68K_REG_A0).toString(16)}`);
            console.log(`  D0: 0x${Module._m68k_get_reg(0, M68K_REG_D0).toString(16)}`);

            // Execute the test sequence
            console.log('\n=== Starting execution ===');
            const cycles = Module._m68k_execute(1000);
            console.log(`Executed ${cycles} cycles`);

            // Report results
            console.log('\n=== Execution Results ===');
            console.log(`PC trace: ${pcTrace.map(pc => `0x${pc.toString(16)}`).join(' -> ')}`);
            console.log(`Total writes: ${allWrites.length}`);
            console.log(`Stack writes: ${stackWrites.length}`);

            console.log('\nStack write details:');
            stackWrites.forEach((write, i) => {
                console.log(`  ${i+1}: addr=0x${write.address.toString(16)}, size=${write.size}, value=0x${write.value.toString(16)}`);
            });

            // Final CPU state
            const finalPC = Module._m68k_get_reg(0, M68K_REG_PC);
            const finalSP = Module._m68k_get_reg(0, M68K_REG_A7);
            console.log(`\nFinal state:`);
            console.log(`  PC: 0x${finalPC.toString(16)}`);
            console.log(`  SP: 0x${finalSP.toString(16)}`);
            console.log(`  SP delta: ${STACK_BASE - finalSP} bytes`);

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
            console.log(`Actual SP delta: ${STACK_BASE - finalSP} bytes`);

            // Check if we reached the target function
            expect(pcTrace).toContain(TARGET_A);

            // Document the behavior for further analysis
            console.log('\n=== Analysis for Bug Report ===');
            console.log(`This test documents the current behavior of the musashi-wasm core.`);
            console.log(`Stack write count: ${stackWrites.length}`);
            console.log(`This data should be compared with TP backend results.`);

        } finally {
            // Cleanup
            if (wasmMemoryPtr) Module._free(wasmMemoryPtr);
            if (readCallbackPtr) Module.removeFunction(readCallbackPtr);
            if (writeCallbackPtr) Module.removeFunction(writeCallbackPtr);
            if (pcHookPtr) Module.removeFunction(pcHookPtr);
            Module.ccall('clear_regions', 'void', [], []);
        }
    });

    it('should provide detailed analysis of movem instruction execution', () => {
        const MEMORY_SIZE = 64 * 1024;

        let wasmMemoryPtr, writeCallbackPtr, pcHookPtr;
        const writeSequence = [];
        const instructionTrace = [];

        try {
            wasmMemoryPtr = Module._malloc(MEMORY_SIZE);
            const jsMemory = Module.HEAPU8.subarray(wasmMemoryPtr, wasmMemoryPtr + MEMORY_SIZE);
            Module.ccall('add_region', 'void', ['number', 'number', 'number'],
                [0x0, MEMORY_SIZE, wasmMemoryPtr]);

            // Just the movem instruction for detailed analysis
            const movemInstr = [0x48, 0xe7, 0xff, 0xfe]; // movem.l D0-D7/A0-A6,-(A7)
            jsMemory.set(movemInstr, 0x400);

            // NOP to stop after movem
            const nopInstr = [0x4e, 0x71];
            jsMemory.set(nopInstr, 0x404);

            // Set reset vectors
            jsMemory[0] = 0x00; jsMemory[1] = 0x00; jsMemory[2] = 0x80; jsMemory[3] = 0x00; // SP
            jsMemory[4] = 0x00; jsMemory[5] = 0x00; jsMemory[6] = 0x04; jsMemory[7] = 0x00; // PC

            // Track writes with timestamps
            writeCallbackPtr = Module.addFunction((address, size, value) => {
                const writeInfo = {
                    address,
                    size,
                    value,
                    timestamp: Date.now(),
                    order: writeSequence.length
                };
                writeSequence.push(writeInfo);
                console.log(`Write ${writeInfo.order}: addr=0x${address.toString(16)}, size=${size}, value=0x${value.toString(16)}`);
            }, 'viii');
            Module.ccall('set_write_mem_func', 'void', ['number'], [writeCallbackPtr]);

            // Track instruction boundaries
            pcHookPtr = Module.addFunction((pc) => {
                instructionTrace.push({
                    pc,
                    writesBefore: writeSequence.length
                });
                console.log(`PC: 0x${pc.toString(16)}, writes so far: ${writeSequence.length}`);

                // Stop after NOP
                if (pc === 0x404) {
                    return 1;
                }
                return 0;
            }, 'ii');
            Module.ccall('set_pc_hook_func', 'void', ['number'], [pcHookPtr]);

            // Initialize and run
            Module._m68k_pulse_reset();
            Module._m68k_execute(50);

            // Analyze the write pattern
            console.log('\n=== MOVEM Write Analysis ===');
            console.log(`Total writes during movem: ${writeSequence.length}`);

            // Group writes by instruction
            const movemWrites = writeSequence.filter((_, i) =>
                i < instructionTrace.find(trace => trace.pc === 0x404)?.writesBefore || writeSequence.length
            );

            console.log(`Writes attributed to movem: ${movemWrites.length}`);

            // Check for write size patterns
            const writeSizes = movemWrites.map(w => w.size);
            const sizeDistribution = writeSizes.reduce((acc, size) => {
                acc[size] = (acc[size] || 0) + 1;
                return acc;
            }, {});

            console.log('Write size distribution:', sizeDistribution);

            // This gives us baseline data for comparison with TP backend
            console.log('\n=== Data for Fusion Comparison ===');
            console.log('WASM Backend Results:');
            console.log(`- Write count: ${movemWrites.length}`);
            console.log(`- Size distribution: ${JSON.stringify(sizeDistribution)}`);
            console.log(`- Address range: 0x${Math.min(...movemWrites.map(w => w.address)).toString(16)} - 0x${Math.max(...movemWrites.map(w => w.address)).toString(16)}`);

        } finally {
            if (wasmMemoryPtr) Module._free(wasmMemoryPtr);
            if (writeCallbackPtr) Module.removeFunction(writeCallbackPtr);
            if (pcHookPtr) Module.removeFunction(pcHookPtr);
            Module.ccall('clear_regions', 'void', [], []);
        }
    });
});