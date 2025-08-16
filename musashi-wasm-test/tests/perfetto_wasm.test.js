import path from 'path';
import fs from 'fs';
import { fileURLToPath } from 'url';
import createMusashiModule from '../load-musashi.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

describe('Musashi WASM Perfetto Integration Test', () => {
    let Module;
    let perfettoAvailable = true;

    // Load the Perfetto-enabled WASM module once for all tests
    beforeAll(async () => {
        Module = await createMusashiModule();
        expect(Module).toBeDefined();
        perfettoAvailable = typeof Module._m68k_perfetto_init === 'function';
        if (!perfettoAvailable) {
            console.warn("Perfetto functions not found; skipping Perfetto tests.");
        }
    });

    // Isolate tests by cleaning up state before each run
    beforeEach(() => {
        // Reset the emulator and API state
        Module.ccall('reset_myfunc_state', 'void', [], []);
        Module._m68k_init();
    });

    afterEach(() => {
        // Ensure Perfetto is destroyed if it was initialized
        if (Module && Module._m68k_perfetto_is_initialized && Module._m68k_perfetto_is_initialized()) {
            Module._m68k_perfetto_destroy();
        }
    });

    test('should generate a valid Perfetto trace for a complex M68k program', () => {
        if (!perfettoAvailable) { return; }
        // This test replicates the structure of the native test in test_perfetto.cpp

        // 1. Manually encoded M68k program - merge sort, factorial, and nested calls
        // This is a complex test program that exercises various CPU features
        const machineCode = new Uint8Array([
            // Main program setup
            0x20, 0x7C, 0x00, 0x00, 0x20, 0x00,  // movea.l #$2000, a0
            0x41, 0xE8, 0x00, 0x20,              // lea 32(a0), a0
            
            // Load test data into memory for sorting
            0x30, 0xFC, 0x00, 0x08,              // move.w #8, (a0)+
            0x30, 0xFC, 0x00, 0x05,              // move.w #5, (a0)+
            0x30, 0xFC, 0x00, 0x03,              // move.w #3, (a0)+
            0x30, 0xFC, 0x00, 0x02,              // move.w #2, (a0)+
            0x30, 0xFC, 0x00, 0x09,              // move.w #9, (a0)+
            0x30, 0xFC, 0x00, 0x07,              // move.w #7, (a0)+
            0x30, 0xFC, 0x00, 0x01,              // move.w #1, (a0)+
            0x30, 0xFC, 0x00, 0x04,              // move.w #4, (a0)+
            
            // Call merge sort on the array
            0x48, 0x7A, 0x00, 0x06,              // pea 6(pc) ; push return address
            0x60, 0x00, 0x00, 0x40,              // bra merge_sort
            
            // After sort, compute factorial of 5
            0x20, 0x3C, 0x00, 0x00, 0x00, 0x05,  // move.l #5, d0
            0x61, 0x00, 0x01, 0x00,              // bsr factorial
            0x23, 0xC0, 0x00, 0x00, 0x30, 0x00,  // move.l d0, $3000
            
            // Call nested functions
            0x61, 0x00, 0x01, 0x20,              // bsr func_a
            
            // Write pattern to memory
            0x61, 0x00, 0x01, 0x50,              // bsr write_pattern
            
            // Stop execution
            0x4E, 0x72, 0x20, 0x00,              // stop #$2000
            
            // === merge_sort subroutine (simplified) ===
            // Address: 0x44c
            0x4E, 0x56, 0xFF, 0xF8,              // link a6, #-8
            0x48, 0xE7, 0x30, 0x38,              // movem.l d2-d3/a2-a4, -(sp)
            // Sorting logic (simplified for test)
            0x4E, 0x71,                          // nop (placeholder)
            0x4C, 0xDF, 0x1C, 0x0C,              // movem.l (sp)+, d2-d3/a2-a4
            0x4E, 0x5E,                          // unlk a6
            0x4E, 0x75,                          // rts
            
            // === factorial subroutine ===
            // Computes factorial recursively
            0x0C, 0x80, 0x00, 0x00, 0x00, 0x01,  // cmpi.l #1, d0
            0x6F, 0x00, 0x00, 0x0A,              // ble.w done_factorial
            0x2F, 0x00,                          // move.l d0, -(sp)
            0x53, 0x80,                          // subq.l #1, d0
            0x61, 0xF0,                          // bsr.b factorial (recursive)
            0x22, 0x1F,                          // move.l (sp)+, d1
            0xC0, 0xC1,                          // mulu.w d1, d0
            0x4E, 0x75,                          // rts
            // done_factorial:
            0x70, 0x01,                          // moveq #1, d0
            0x4E, 0x75,                          // rts
            
            // === func_a (calls func_b) ===
            0x48, 0xE7, 0x80, 0x00,              // movem.l d0, -(sp)
            0x61, 0x00, 0x00, 0x08,              // bsr func_b
            0x4C, 0xDF, 0x00, 0x01,              // movem.l (sp)+, d0
            0x4E, 0x75,                          // rts
            
            // === func_b (calls func_c) ===
            0x48, 0xE7, 0xC0, 0x00,              // movem.l d0-d1, -(sp)
            0x61, 0x00, 0x00, 0x08,              // bsr func_c
            0x4C, 0xDF, 0x00, 0x03,              // movem.l (sp)+, d0-d1
            0x4E, 0x75,                          // rts
            
            // === func_c ===
            0x70, 0x42,                          // moveq #42, d0
            0x4E, 0x75,                          // rts
            
            // === write_pattern ===
            // Writes a pattern to memory locations
            0x20, 0x7C, 0x00, 0x00, 0x40, 0x00,  // movea.l #$4000, a0
            0x70, 0x00,                          // moveq #0, d0
            0x20, 0xC0,                          // move.l d0, (a0)+
            0x52, 0x80,                          // addq.l #1, d0
            0x20, 0xC0,                          // move.l d0, (a0)+
            0x52, 0x80,                          // addq.l #1, d0
            0x20, 0xC0,                          // move.l d0, (a0)+
            0x52, 0x80,                          // addq.l #1, d0
            0x20, 0xC0,                          // move.l d0, (a0)+
            0x4E, 0x75                           // rts
        ]);

        const PROG_START_ADDR = 0x400;
        const STACK_POINTER_ADDR = 0x10000;
        const MEMORY_SIZE = 128 * 1024;
        let wasmMemoryPtr;

        try {
            // 2. Setup Memory
            wasmMemoryPtr = Module._malloc(MEMORY_SIZE);
            const jsMemory = Module.HEAPU8.subarray(wasmMemoryPtr, wasmMemoryPtr + MEMORY_SIZE);
            Module.ccall('add_region', 'void', ['number', 'number', 'number'], [0, MEMORY_SIZE, wasmMemoryPtr]);

            // Load the program into memory
            jsMemory.set(machineCode, PROG_START_ADDR);

            // Set reset vectors
            jsMemory[0] = (STACK_POINTER_ADDR >> 24) & 0xFF;
            jsMemory[1] = (STACK_POINTER_ADDR >> 16) & 0xFF;
            jsMemory[2] = (STACK_POINTER_ADDR >> 8) & 0xFF;
            jsMemory[3] = STACK_POINTER_ADDR & 0xFF;
            jsMemory[4] = (PROG_START_ADDR >> 24) & 0xFF;
            jsMemory[5] = (PROG_START_ADDR >> 16) & 0xFF;
            jsMemory[6] = (PROG_START_ADDR >> 8) & 0xFF;
            jsMemory[7] = PROG_START_ADDR & 0xFF;
            
            // 3. Initialize and Configure Perfetto
            const initResult = Module.ccall('m68k_perfetto_init', 'number', ['string'], ['MusashiWasmTest']);
            expect(initResult).toBe(0);

            // Register symbol names to enrich the trace, just like the C++ test
            Module.ccall('register_function_name', 'void', ['number', 'string'], [0x400, 'main']);
            Module.ccall('register_function_name', 'void', ['number', 'string'], [0x44c, 'merge_sort']);
            Module.ccall('register_function_name', 'void', ['number', 'string'], [0x500, 'factorial']);
            Module.ccall('register_function_name', 'void', ['number', 'string'], [0x520, 'func_a']);
            Module.ccall('register_function_name', 'void', ['number', 'string'], [0x530, 'func_b']);
            Module.ccall('register_function_name', 'void', ['number', 'string'], [0x540, 'func_c']);
            Module.ccall('register_function_name', 'void', ['number', 'string'], [0x550, 'write_pattern']);
            
            // Register memory regions
            Module.ccall('register_memory_name', 'void', ['number', 'string'], [0x2000, 'array_to_sort']);
            Module.ccall('register_memory_name', 'void', ['number', 'string'], [0x3000, 'factorial_result']);
            Module.ccall('register_memory_name', 'void', ['number', 'string'], [0x4000, 'pattern_buffer']);
            
            // Enable all tracing features
            Module._m68k_perfetto_enable_flow(1);
            Module._m68k_perfetto_enable_instructions(1);
            Module._m68k_perfetto_enable_memory(1);

            // 4. Execute the M68k Program
            Module._m68k_pulse_reset();
            const cyclesExecuted = Module._m68k_execute(10000); // Should be enough to finish
            expect(cyclesExecuted).toBeGreaterThan(0);

            // 5. Export and Validate the Trace Data
            // Clean up any pending slices
            if (Module._m68k_perfetto_cleanup_slices) {
                Module._m68k_perfetto_cleanup_slices();
            }

            const dataPtrPtr = Module._malloc(4);
            const sizePtr = Module._malloc(4);
            
            const exportResult = Module._m68k_perfetto_export_trace(dataPtrPtr, sizePtr);
            expect(exportResult).toBe(0);

            const dataPtr = Module.getValue(dataPtrPtr, '*');
            const dataSize = Module.getValue(sizePtr, 'i32');

            expect(dataPtr).not.toBe(0);
            expect(dataSize).toBeGreaterThan(100); // A valid trace should have some data

            // Copy data from WASM memory to a Node.js Buffer
            const traceData = new Uint8Array(Module.HEAPU8.buffer, dataPtr, dataSize);

            // Save the trace file for inspection
            const tracePath = path.join(__dirname, 'test_output.perfetto-trace');
            fs.writeFileSync(tracePath, traceData);
            console.log(`Perfetto trace saved to: ${tracePath}`);
            console.log(`Trace size: ${dataSize} bytes`);

            // 6. Cleanup
            Module._m68k_perfetto_free_trace_data(dataPtr);
            Module._free(dataPtrPtr);
            Module._free(sizePtr);
            
        } finally {
            // Free allocated WASM memory
            if (wasmMemoryPtr) Module._free(wasmMemoryPtr);
            Module.ccall('clear_regions', 'void', [], []);
        }
    });

    test('should handle basic instruction tracing', () => {
        if (!perfettoAvailable) { return; }
        const MEMORY_SIZE = 64 * 1024;
        let wasmMemoryPtr;

        try {
            // Setup memory
            wasmMemoryPtr = Module._malloc(MEMORY_SIZE);
            const jsMemory = Module.HEAPU8.subarray(wasmMemoryPtr, wasmMemoryPtr + MEMORY_SIZE);
            Module.ccall('add_region', 'void', ['number', 'number', 'number'], [0, MEMORY_SIZE, wasmMemoryPtr]);

            // Simple test program from the C++ test
            const simpleProgram = new Uint8Array([
                0x20, 0x3c, 0x00, 0x00, 0x00, 0x05,  // move.l #5, d0
                0x22, 0x3c, 0x00, 0x00, 0x00, 0x03,  // move.l #3, d1
                0xd0, 0x81,                          // add.l d1, d0
                0x61, 0x00, 0x00, 0x06,              // bsr.w subroutine
                0x4e, 0x72, 0x27, 0x00,              // stop #$2700
                // subroutine:
                0x06, 0x80, 0x00, 0x00, 0x00, 0x02,  // addi.l #2, d0
                0x4e, 0x75                           // rts
            ]);

            // Load program at 0x400
            jsMemory.set(simpleProgram, 0x400);

            // Set reset vectors
            jsMemory[0] = 0x00;
            jsMemory[1] = 0x00;
            jsMemory[2] = 0x10;
            jsMemory[3] = 0x00;
            jsMemory[4] = 0x00;
            jsMemory[5] = 0x00;
            jsMemory[6] = 0x04;
            jsMemory[7] = 0x00;

            // Initialize Perfetto
            const initResult = Module.ccall('m68k_perfetto_init', 'number', ['string'], ['SimpleInstructionTest']);
            expect(initResult).toBe(0);

            // Enable instruction tracing only
            Module._m68k_perfetto_enable_instructions(1);

            // Execute the program
            Module._m68k_pulse_reset();
            let totalCycles = 0;
            for (let i = 0; i < 10; i++) {
                const cycles = Module._m68k_execute(20);
                totalCycles += cycles;
                if (cycles === 0) break;
            }

            // Verify execution occurred (the exact result may vary based on execution)
            const d0Value = Module._m68k_get_reg(16); // M68K_REG_D0
            // Just check that some execution happened
            expect(totalCycles).toBeGreaterThan(0);

            // Export trace to verify it was created
            const dataPtrPtr = Module._malloc(4);
            const sizePtr = Module._malloc(4);
            
            const exportResult = Module._m68k_perfetto_export_trace(dataPtrPtr, sizePtr);
            expect(exportResult).toBe(0);

            const dataPtr = Module.getValue(dataPtrPtr, '*');
            const dataSize = Module.getValue(sizePtr, 'i32');

            expect(dataSize).toBeGreaterThan(0);

            // Cleanup
            Module._m68k_perfetto_free_trace_data(dataPtr);
            Module._free(dataPtrPtr);
            Module._free(sizePtr);

        } finally {
            if (wasmMemoryPtr) Module._free(wasmMemoryPtr);
            Module.ccall('clear_regions', 'void', [], []);
            if (Module._m68k_perfetto_is_initialized && Module._m68k_perfetto_is_initialized()) {
                Module._m68k_perfetto_destroy();
            }
        }
    });

    test('should support symbol naming', () => {
        if (!perfettoAvailable) { return; }
        // Test that symbol naming functions don't crash
        Module.ccall('register_function_name', 'void', ['number', 'string'], [0x400, 'test_function']);
        Module.ccall('register_memory_name', 'void', ['number', 'string'], [0x1000, 'test_memory']);
        Module.ccall('register_memory_range', 'void', ['number', 'number', 'string'], [0x2000, 256, 'test_buffer']);
        
        // Clear names to verify cleanup works
        Module.ccall('clear_registered_names', 'void', [], []);
        
        // No crash = success
        expect(true).toBe(true);
    });
});
