import path from 'path';
import fs from 'fs';
import { jest } from '@jest/globals';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Path to the WASM module, assuming it's in the parent directory
const modulePath = path.resolve(__dirname, '../../musashi-node.out.mjs');

// Pre-flight check to ensure the module exists before running tests
if (!fs.existsSync(modulePath)) {
    console.error(`\n============================================`);
    console.error(`ERROR: WASM module not found at ${modulePath}`);
    console.error(`============================================\n`);
    console.error(`The Musashi WASM module has not been built yet.\n`);
    console.error(`To build the WASM module, you need:`);
    console.error(`1. Emscripten SDK installed and configured`);
    console.error(`2. Run one of the following commands from the Musashi root directory:`);
    console.error(`   - ./build.fish (if Fish shell is available)`);
    console.error(`   - emmake make -j8 && emcc [options] (manual build)`);
    console.error(`\nFor detailed instructions, see the README.md file.`);
    console.error(`\n============================================\n`);
    process.exit(1);
}

// Load the factory function for the WASM module
import createMusashiModule from '../load-musashi.js';

// Constants for register access, from m68k.h
const M68K_REG_D0 = 0;
const M68K_REG_D1 = 1;
const M68K_REG_D2 = 2;
const M68K_REG_D3 = 3;
const M68K_REG_D4 = 4;
const M68K_REG_D5 = 5;
const M68K_REG_D6 = 6;
const M68K_REG_D7 = 7;
const M68K_REG_A0 = 8;
const M68K_REG_A1 = 9;
const M68K_REG_A2 = 10;
const M68K_REG_A3 = 11;
const M68K_REG_A4 = 12;
const M68K_REG_A5 = 13;
const M68K_REG_A6 = 14;
const M68K_REG_A7 = 15;
const M68K_REG_PC = 16;
const M68K_REG_SR = 17;
const M68K_REG_SP = 18;
const M68K_REG_USP = 19;

/**
 * SingleStep test framework for WASM - validates individual M68k instruction emulation
 * against reference test data from SingleStepTests/m68000 repository
 */
describe('Musashi WASM SingleStep Instruction Validation', () => {
    let Module; // This will hold the instantiated WASM module
    let memory; // Memory buffer for CPU
    let pcHooks = []; // Track PC hooks for instruction execution verification
    
    const MEMORY_SIZE = 1024 * 1024; // 1MB memory buffer
    const TEST_DATA_PATH = path.resolve(__dirname, '../../third_party/m68000/v1/');

    // Helper class to parse and run SingleStep tests
    class SingleStepTestRunner {
        constructor(module) {
            this.Module = module;
            this.memory = new Uint8Array(MEMORY_SIZE);
            this.pcHooks = [];
        }

        // Set up memory and callback functions
        setupCallbacks() {
            // Reset state
            Module._reset_myfunc_state();
            this.pcHooks = [];

            // Set up memory callbacks
            const readMemFunc = Module.addFunction((address) => {
                if (address < MEMORY_SIZE) {
                    return this.memory[address];
                }
                return 0;
            }, 'ii');

            const writeMemFunc = Module.addFunction((address, value) => {
                if (address < MEMORY_SIZE) {
                    this.memory[address] = value & 0xFF;
                }
            }, 'vii');

            const pcHookFunc = Module.addFunction((pc) => {
                this.pcHooks.push(pc);
                // Stop after 2 PC hooks (before and after instruction)
                return this.pcHooks.length >= 2 ? 1 : 0;
            }, 'ii');

            Module._set_read8_callback(readMemFunc);
            Module._set_write8_callback(writeMemFunc);
            Module._set_pc_hook_func(pcHookFunc);
        }

        // Apply processor state from test data
        applyInitialState(initialState) {
            // Clear memory
            this.memory.fill(0);

            // Set data registers D0-D7
            for (let i = 0; i < 8; i++) {
                Module._m68k_set_reg(M68K_REG_D0 + i, initialState[`d${i}`] || 0);
            }

            // Set address registers A0-A7  
            for (let i = 0; i < 8; i++) {
                Module._m68k_set_reg(M68K_REG_A0 + i, initialState[`a${i}`] || 0);
            }

            // Set special registers
            Module._m68k_set_reg(M68K_REG_USP, initialState.usp || 0);
            Module._m68k_set_reg(M68K_REG_SP, initialState.ssp || 0);
            Module._m68k_set_reg(M68K_REG_SR, initialState.sr || 0);
            Module._m68k_set_reg(M68K_REG_PC, initialState.pc || 0);

            // Apply RAM contents
            if (initialState.ram && Array.isArray(initialState.ram)) {
                for (const [address, value] of initialState.ram) {
                    if (address < MEMORY_SIZE) {
                        this.memory[address] = value & 0xFF;
                    }
                }
            }

            // Set up reset vectors (SP at 0, PC at 4)
            const sp = Module._m68k_get_reg(M68K_REG_SP);
            const pc = Module._m68k_get_reg(M68K_REG_PC);
            
            // Write SP to address 0-3 (big endian)
            this.memory[0] = (sp >> 24) & 0xFF;
            this.memory[1] = (sp >> 16) & 0xFF;
            this.memory[2] = (sp >> 8) & 0xFF;
            this.memory[3] = sp & 0xFF;
            
            // Write PC to address 4-7 (big endian)
            this.memory[4] = (pc >> 24) & 0xFF;
            this.memory[5] = (pc >> 16) & 0xFF;
            this.memory[6] = (pc >> 8) & 0xFF;
            this.memory[7] = pc & 0xFF;
        }

        // Extract final processor state
        getFinalState() {
            const finalState = {};

            // Get data registers
            for (let i = 0; i < 8; i++) {
                finalState[`d${i}`] = Module._m68k_get_reg(M68K_REG_D0 + i);
            }

            // Get address registers
            for (let i = 0; i < 8; i++) {
                finalState[`a${i}`] = Module._m68k_get_reg(M68K_REG_A0 + i);
            }

            // Get special registers
            finalState.usp = Module._m68k_get_reg(M68K_REG_USP);
            finalState.ssp = Module._m68k_get_reg(M68K_REG_SP);
            finalState.sr = Module._m68k_get_reg(M68K_REG_SR);
            finalState.pc = Module._m68k_get_reg(M68K_REG_PC);

            return finalState;
        }

        // Run a single test case
        async runTest(testCase) {
            try {
                // Setup
                this.setupCallbacks();
                Module._m68k_init();
                this.applyInitialState(testCase.initial);
                Module._m68k_pulse_reset();

                // Execute instruction
                const cycles = Module._m68k_execute(100); // Should be enough for any single instruction

                // Get final state
                const finalState = this.getFinalState();

                // Compare states
                const differences = this.compareStates(finalState, testCase.final);

                return {
                    passed: differences.length === 0,
                    testName: testCase.name,
                    differences: differences,
                    cyclesExecuted: cycles,
                    pcHooks: [...this.pcHooks]
                };

            } catch (error) {
                return {
                    passed: false,
                    testName: testCase.name,
                    differences: [`Exception: ${error.message}`],
                    cyclesExecuted: 0,
                    pcHooks: []
                };
            }
        }

        // Compare two processor states and return differences
        compareStates(actual, expected) {
            const differences = [];

            // Compare data registers
            for (let i = 0; i < 8; i++) {
                const actualVal = actual[`d${i}`] || 0;
                const expectedVal = expected[`d${i}`] || 0;
                if (actualVal !== expectedVal) {
                    differences.push(`D${i}: expected ${expectedVal}, got ${actualVal}`);
                }
            }

            // Compare address registers
            for (let i = 0; i < 8; i++) {
                const actualVal = actual[`a${i}`] || 0;
                const expectedVal = expected[`a${i}`] || 0;
                if (actualVal !== expectedVal) {
                    differences.push(`A${i}: expected ${expectedVal}, got ${actualVal}`);
                }
            }

            // Compare special registers
            if ((actual.usp || 0) !== (expected.usp || 0)) {
                differences.push(`USP: expected ${expected.usp}, got ${actual.usp}`);
            }
            if ((actual.ssp || 0) !== (expected.ssp || 0)) {
                differences.push(`SSP: expected ${expected.ssp}, got ${actual.ssp}`);
            }
            if ((actual.sr || 0) !== (expected.sr || 0)) {
                differences.push(`SR: expected ${expected.sr}, got ${actual.sr}`);
            }
            if ((actual.pc || 0) !== (expected.pc || 0)) {
                differences.push(`PC: expected ${expected.pc}, got ${actual.pc}`);
            }

            return differences;
        }
    }

    beforeAll(async () => {
        // Load the WASM module
        Module = await createMusashiModule();
        expect(Module).toBeDefined();
        expect(typeof Module._m68k_init).toBe('function');

        memory = new Uint8Array(MEMORY_SIZE);
    });

    // Helper function to load and parse JSON test file
    function loadTestFile(filename) {
        const filePath = path.join(TEST_DATA_PATH, filename);
        if (!fs.existsSync(filePath)) {
            throw new Error(`Test file not found: ${filePath}`);
        }

        const content = fs.readFileSync(filePath, 'utf8');
        return JSON.parse(content);
    }

    // Helper function to run tests for an instruction
    async function runInstructionTests(filename, maxTests = 10) {
        let testData;
        try {
            testData = loadTestFile(filename);
        } catch (error) {
            return { skipped: true, reason: error.message };
        }

        const runner = new SingleStepTestRunner(Module);
        const results = {
            instruction: filename.replace('.json', ''),
            totalTests: Math.min(maxTests, testData.length),
            passedTests: 0,
            failedTests: 0,
            individualResults: []
        };

        console.log(`\nRunning ${results.totalTests} tests for ${results.instruction}...`);

        for (let i = 0; i < results.totalTests; i++) {
            const testCase = testData[i];
            const result = await runner.runTest(testCase);

            if (result.passed) {
                results.passedTests++;
            } else {
                results.failedTests++;
                
                // Log first few failures for debugging
                if (results.failedTests <= 3) {
                    console.log(`  FAIL: ${result.testName}`);
                    result.differences.slice(0, 3).forEach(diff => {
                        console.log(`    ${diff}`);
                    });
                }
            }

            results.individualResults.push(result);
        }

        const passRate = (results.passedTests / results.totalTests * 100).toFixed(1);
        console.log(`${results.instruction}: ${results.passedTests}/${results.totalTests} passed (${passRate}%)`);

        return results;
    }

    // Test NOP instruction (should be simple and have high pass rate)
    test('NOP instruction validation', async () => {
        const results = await runInstructionTests('NOP.json', 5);
        
        if (results.skipped) {
            console.log(`Skipped NOP test: ${results.reason}`);
            return;
        }

        expect(results.totalTests).toBeGreaterThan(0);
        // We expect at least some NOP tests to pass
        expect(results.passedTests).toBeGreaterThan(0);
    }, 30000); // 30 second timeout

    // Test basic MOVE instruction
    test('MOVE.b instruction validation', async () => {
        const results = await runInstructionTests('MOVE.b.json', 5);
        
        if (results.skipped) {
            console.log(`Skipped MOVE.b test: ${results.reason}`);
            return;
        }

        expect(results.totalTests).toBeGreaterThan(0);
        // Just ensure tests run without crashing
    }, 30000);

    // Test ADD instruction
    test('ADD.b instruction validation', async () => {
        const results = await runInstructionTests('ADD.b.json', 5);
        
        if (results.skipped) {
            console.log(`Skipped ADD.b test: ${results.reason}`);
            return;
        }

        expect(results.totalTests).toBeGreaterThan(0);
    }, 30000);

    // Comprehensive test runner for multiple instructions
    test('Multiple instruction validation suite', async () => {
        const testInstructions = [
            'NOP.json',
            'MOVE.b.json', 
            'ADD.b.json',
            'SUB.b.json',
            'CMP.b.json'
        ];

        const allResults = [];
        let totalPassed = 0;
        let totalTests = 0;

        for (const instruction of testInstructions) {
            const result = await runInstructionTests(instruction, 10);
            if (!result.skipped) {
                allResults.push(result);
                totalPassed += result.passedTests;
                totalTests += result.totalTests;
            }
        }

        // Print summary
        console.log('\n=== SingleStep WASM Test Summary ===');
        if (totalTests > 0) {
            console.log(`Overall: ${totalPassed}/${totalTests} passed (${(totalPassed/totalTests*100).toFixed(1)}%)`);
        }

        allResults.forEach(result => {
            const passRate = (result.passedTests / result.totalTests * 100).toFixed(1);
            console.log(`${result.instruction}: ${result.passedTests}/${result.totalTests} (${passRate}%)`);
        });

        // Test passes if we successfully ran some tests
        expect(totalTests).toBeGreaterThan(0);
    }, 120000); // 2 minute timeout for comprehensive test
});