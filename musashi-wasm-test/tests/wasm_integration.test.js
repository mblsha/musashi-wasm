const path = require('path');
const fs = require('fs');

// Path to the WASM module, assuming it's in the parent directory
const modulePath = path.resolve(__dirname, '../../musashi-node.out.js');

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
// Try to use the wrapper if it exists, otherwise load directly
let createMusashiModule;
try {
    // Try the wrapper first (for local development)
    createMusashiModule = require('../load-musashi.js');
} catch (e) {
    // Fallback to direct require (for CI or when wrapper doesn't exist)
    try {
        createMusashiModule = require(modulePath);
    } catch (requireError) {
        // If direct require fails due to ES6 syntax, we need to handle it
        console.error('Failed to load module directly. Error:', requireError.message);
        console.error('The module may have mixed CommonJS/ES6 exports.');
        process.exit(1);
    }
}

// Constants for register access, from m68k.h
const M68K_REG_D0 = 0;
const M68K_REG_PC = 16;
const M68K_REG_SP = 18;

describe('Musashi WASM Node.js Integration Test', () => {
    let Module; // This will hold the instantiated WASM module

    // 1. Requirement: Module Loading and Instantiation
    beforeAll(async () => {
        // The module exports a factory function that returns a Promise
        Module = await createMusashiModule();
        expect(Module).toBeDefined();
        expect(typeof Module._m68k_init).toBe('function');
    });

    // Reset the CPU state before each test for isolation
    beforeEach(() => {
        Module._m68k_init();
        // Clear regions between tests for proper cleanup
        Module.ccall('clear_regions', 'void', [], []);
    });

    it('should correctly execute a simple program using the full API bridge', () => {
        // --- Test Scenario from PRD ---
        // Program:
        //   MOVE.L #$DEADBEEF, D0
        //   MOVE.L D0, $10000
        //   STOP #$2700

        const machineCode = new Uint8Array([
            0x20, 0x3C, 0xDE, 0xAD, 0xBE, 0xEF, // MOVE.L #$DEADBEEF, D0 (6 bytes)
            0x23, 0xC0, 0x00, 0x01, 0x00, 0x00, // MOVE.L D0, $10000   (6 bytes)
            0x4E, 0x72, 0x27, 0x00              // STOP #$2700         (4 bytes)
        ]);

        const MEMORY_SIZE = 64 * 1024; // 64KB
        const PROG_START_ADDR = 0x400;
        const STACK_POINTER_ADDR = 0x8000;
        const DATA_TARGET_ADDR = 0x10000; // Note: Outside the mapped region

        // 2. Requirement: Memory Callbacks & Regions
        let jsMemory, wasmMemoryPtr, writeCallbackPtr;
        let pcHookPtr;

        try {
            // Allocate memory inside the WASM module heap for our main RAM
            wasmMemoryPtr = Module._malloc(MEMORY_SIZE);
            expect(wasmMemoryPtr).not.toBe(0);

            // Get a view of the WASM memory to manipulate it from JS
            jsMemory = Module.HEAPU8.subarray(wasmMemoryPtr, wasmMemoryPtr + MEMORY_SIZE);

            // Map this WASM memory as a region for fast access
            Module.ccall('add_region', 'void', ['number', 'number', 'number'], [0x0, MEMORY_SIZE, wasmMemoryPtr]);

            // Set up write callback for memory outside the region
            const writeCallbackSpy = jest.fn();
            writeCallbackPtr = Module.addFunction((address, size, value) => {
                writeCallbackSpy(address, size, value);
            }, 'viii');
            Module.ccall('set_write_mem_func', 'void', ['number'], [writeCallbackPtr]);

            // 3. Requirement: Instruction Hooking
            const pcLog = [];
            const pcHookSpy = jest.fn((pc) => {
                pcLog.push(pc);
                return 0; // Continue execution
            });
            pcHookPtr = Module.addFunction(pcHookSpy, 'ii');
            Module.ccall('set_pc_hook_func', 'void', ['number'], [pcHookPtr]);

            // --- Test Setup ---
            // Write program to memory
            jsMemory.set(machineCode, PROG_START_ADDR);

            // Set reset vectors (big-endian format)
            const spBytes = new Uint8Array(new Uint32Array([STACK_POINTER_ADDR]).buffer).reverse();
            const pcBytes = new Uint8Array(new Uint32Array([PROG_START_ADDR]).buffer).reverse();
            jsMemory.set(spBytes, 0);
            jsMemory.set(pcBytes, 4);

            // 4. Requirement: Core CPU Control
            Module._m68k_pulse_reset();

            // Verify reset vectors were loaded correctly
            expect(Module._m68k_get_reg(M68K_REG_SP)).toBe(STACK_POINTER_ADDR);
            expect(Module._m68k_get_reg(M68K_REG_PC)).toBe(PROG_START_ADDR);

            // Execute the program
            const cycles = Module._m68k_execute(100);
            expect(cycles).toBeGreaterThan(0);

            // --- Verification ---
            // 5. Requirement: Register Access
            expect(Module._m68k_get_reg(M68K_REG_D0)).toBe(0xDEADBEEF);

            // 6. Verify Memory Callback was triggered
            expect(writeCallbackSpy).toHaveBeenCalledTimes(1);
            expect(writeCallbackSpy).toHaveBeenCalledWith(DATA_TARGET_ADDR, 4, 0xDEADBEEF);
            
            // 7. Verify Instruction Hook
            expect(pcHookSpy).toHaveBeenCalledTimes(3);
            expect(pcLog).toEqual([
                0x400, // MOVE.L #imm, D0
                0x406, // MOVE.L D0, addr
                0x40c  // STOP
            ]);

        } finally {
            // Clean up WASM heap and function table
            if (wasmMemoryPtr) Module._free(wasmMemoryPtr);
            if (writeCallbackPtr) Module.removeFunction(writeCallbackPtr);
            if (pcHookPtr) Module.removeFunction(pcHookPtr);
        }
    });

    it('should disassemble an instruction correctly', () => {
        // Test NOP instruction disassembly
        const NOP_OPCODE = [0x4E, 0x71];
        
        // Allocate memory for the instruction
        const instPtr = Module._malloc(2);
        Module.HEAPU8.set(NOP_OPCODE, instPtr);

        // Allocate a buffer for the result string
        const bufferSize = 100;
        const bufferPtr = Module._malloc(bufferSize);
        
        try {
            Module.ccall('m68k_disassemble', 'number', ['number', 'number', 'number'], 
                [bufferPtr, instPtr, 0]); // CPU Type 0 (68000)
            const result = Module.UTF8ToString(bufferPtr);
            
            // Disassembler adds tabs and spacing, so we check for the core part
            expect(result.toLowerCase()).toMatch(/nop/);
        } finally {
            Module._free(instPtr);
            Module._free(bufferPtr);
        }
    });

    it('should handle memory regions correctly', () => {
        const REGION_SIZE = 16 * 1024; // 16KB
        const REGION_BASE = 0x2000;
        
        let regionMemPtr;
        let readCallbackPtr, writeCallbackPtr;
        
        try {
            // Allocate memory for the region
            regionMemPtr = Module._malloc(REGION_SIZE);
            const regionView = Module.HEAPU8.subarray(regionMemPtr, regionMemPtr + REGION_SIZE);
            
            // Add the region
            Module.ccall('add_region', 'void', ['number', 'number', 'number'], 
                [REGION_BASE, REGION_SIZE, regionMemPtr]);
            
            // Set up callbacks for addresses outside regions
            const readCallbackSpy = jest.fn((address, size) => {
                return 0xAAAA; // Return a test value
            });
            const writeCallbackSpy = jest.fn();
            
            readCallbackPtr = Module.addFunction(readCallbackSpy, 'iii');
            writeCallbackPtr = Module.addFunction(writeCallbackSpy, 'viii');
            
            Module.ccall('set_read_mem_func', 'void', ['number'], [readCallbackPtr]);
            Module.ccall('set_write_mem_func', 'void', ['number'], [writeCallbackPtr]);
            
            // Write test data to region
            const testData = 0xDEADBEEF;
            regionView[0] = (testData >> 24) & 0xFF;
            regionView[1] = (testData >> 16) & 0xFF;
            regionView[2] = (testData >> 8) & 0xFF;
            regionView[3] = testData & 0xFF;
            
            // Test reading from region (should not trigger callback)
            Module._m68k_init();
            Module._m68k_set_reg(M68K_REG_PC, REGION_BASE);
            Module._m68k_execute(1);
            
            // The read from the region should not trigger the callback
            expect(readCallbackSpy).not.toHaveBeenCalled();
            
            // Test reading from outside region (should trigger callback)
            Module._m68k_set_reg(M68K_REG_PC, 0x100000);
            Module._m68k_execute(1);
            
            // This should trigger the callback
            expect(readCallbackSpy).toHaveBeenCalled();
            
        } finally {
            // Clean up
            if (regionMemPtr) Module._free(regionMemPtr);
            if (readCallbackPtr) Module.removeFunction(readCallbackPtr);
            if (writeCallbackPtr) Module.removeFunction(writeCallbackPtr);
            Module.ccall('clear_regions', 'void', [], []);
        }
    });

    it('should track cycle counts correctly', () => {
        const CYCLES_TO_RUN = 50;
        
        // Initialize CPU
        Module._m68k_init();
        Module._m68k_pulse_reset();
        
        // Run for specific number of cycles
        const actualCycles = Module._m68k_execute(CYCLES_TO_RUN);
        
        // Should have executed some cycles
        expect(actualCycles).toBeGreaterThan(0);
        expect(actualCycles).toBeLessThanOrEqual(CYCLES_TO_RUN);
        
        // Get current cycle count
        const totalCycles = Module._m68k_cycles_run();
        expect(totalCycles).toBe(actualCycles);
    });

    it('should support all exported register access functions', () => {
        Module._m68k_init();
        
        // Test setting and getting a data register
        const testValue = 0x12345678;
        Module._m68k_set_reg(M68K_REG_D0, testValue);
        expect(Module._m68k_get_reg(M68K_REG_D0)).toBe(testValue);
        
        // Test PC register
        const pcValue = 0x1000;
        Module._m68k_set_reg(M68K_REG_PC, pcValue);
        expect(Module._m68k_get_reg(M68K_REG_PC)).toBe(pcValue);
        
        // Test SP register
        const spValue = 0x8000;
        Module._m68k_set_reg(M68K_REG_SP, spValue);
        expect(Module._m68k_get_reg(M68K_REG_SP)).toBe(spValue);
    });

    it('should handle instruction hook interruption correctly', () => {
        const MEMORY_SIZE = 64 * 1024;
        let memPtr, pcHookPtr;
        
        try {
            // Set up memory
            memPtr = Module._malloc(MEMORY_SIZE);
            const memory = Module.HEAPU8.subarray(memPtr, memPtr + MEMORY_SIZE);
            Module.ccall('add_region', 'void', ['number', 'number', 'number'], [0, MEMORY_SIZE, memPtr]);
            
            // Write several NOP instructions
            for (let i = 0x400; i < 0x410; i += 2) {
                memory[i] = 0x4E;
                memory[i + 1] = 0x71;
            }
            
            // Set reset vectors
            memory[0] = 0x00; memory[1] = 0x00; memory[2] = 0x80; memory[3] = 0x00; // SP
            memory[4] = 0x00; memory[5] = 0x00; memory[6] = 0x04; memory[7] = 0x00; // PC
            
            // Set up hook that stops after 3 instructions
            let instructionCount = 0;
            pcHookPtr = Module.addFunction((pc) => {
                instructionCount++;
                return instructionCount >= 3 ? 1 : 0; // Stop after 3 instructions
            }, 'ii');
            Module.ccall('set_pc_hook_func', 'void', ['number'], [pcHookPtr]);
            
            // Reset and execute
            Module._m68k_pulse_reset();
            const cycles = Module._m68k_execute(1000); // Should stop early
            
            // Verify it stopped after 3 instructions
            expect(instructionCount).toBe(3);
            expect(cycles).toBeGreaterThan(0);
            expect(cycles).toBeLessThan(1000); // Didn't use all cycles
            
        } finally {
            if (memPtr) Module._free(memPtr);
            if (pcHookPtr) Module.removeFunction(pcHookPtr);
            Module.ccall('clear_regions', 'void', [], []);
        }
    });
});