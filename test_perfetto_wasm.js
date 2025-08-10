#!/usr/bin/env node

/**
 * Test script to verify Perfetto-enabled WASM build functionality
 * This script tests that the Perfetto tracing functions are properly exported
 * and can be called without errors.
 */

const fs = require('fs');
const path = require('path');

// Import the Node.js version of the WASM module
const musashiPath = path.join(__dirname, 'musashi-node.out.js');

if (!fs.existsSync(musashiPath)) {
    console.error('ERROR: musashi-node.out.js not found. Build the WASM module first.');
    process.exit(1);
}

console.log('Loading Musashi WASM module with Perfetto support...');

// Import the module
const Musashi = require(musashiPath);

async function testPerfettoWasm() {
    try {
        // Wait for module to be ready
        const Module = await Musashi();
        
        console.log('✓ WASM module loaded successfully');
        
        // Test core M68k functions are available
        const coreFunctions = [
            '_m68k_init',
            '_m68k_execute', 
            '_m68k_get_reg',
            '_m68k_set_reg',
            '_m68k_disassemble'
        ];
        
        console.log('\nTesting core M68k functions:');
        for (const func of coreFunctions) {
            if (typeof Module[func] === 'function') {
                console.log(`✓ ${func} is available`);
            } else {
                console.error(`✗ ${func} is missing`);
                process.exit(1);
            }
        }
        
        // Test Perfetto functions are available
        const perfettoFunctions = [
            '_m68k_perfetto_init',
            '_m68k_perfetto_destroy',
            '_m68k_perfetto_enable_flow',
            '_m68k_perfetto_enable_memory', 
            '_m68k_perfetto_enable_instructions',
            '_m68k_perfetto_export_trace',
            '_m68k_perfetto_free_trace_data',
            '_m68k_perfetto_save_trace',
            '_m68k_perfetto_is_initialized',
            '_m68k_perfetto_cleanup_slices'
        ];
        
        console.log('\nTesting Perfetto functions:');
        for (const func of perfettoFunctions) {
            if (typeof Module[func] === 'function') {
                console.log(`✓ ${func} is available`);
            } else {
                console.error(`✗ ${func} is missing`);
                process.exit(1);
            }
        }
        
        // Test basic Perfetto functionality
        console.log('\nTesting basic Perfetto functionality:');
        
        // Initialize Perfetto
        const initResult = Module._m68k_perfetto_init();
        if (initResult === 0) {
            console.log('✓ Perfetto initialization successful');
        } else {
            console.error(`✗ Perfetto initialization failed with code: ${initResult}`);
            process.exit(1);
        }
        
        // Check if initialized
        const isInitialized = Module._m68k_perfetto_is_initialized();
        if (isInitialized) {
            console.log('✓ Perfetto is initialized');
        } else {
            console.error('✗ Perfetto reports not initialized');
            process.exit(1);
        }
        
        // Enable some tracing features
        Module._m68k_perfetto_enable_flow(1);
        Module._m68k_perfetto_enable_memory(1);
        Module._m68k_perfetto_enable_instructions(1);
        console.log('✓ Perfetto tracing features enabled');
        
        // Initialize M68k CPU
        Module._m68k_init();
        console.log('✓ M68k CPU initialized');
        
        // Cleanup
        Module._m68k_perfetto_destroy();
        console.log('✓ Perfetto cleanup successful');
        
        console.log('\n🎉 All tests passed! Perfetto WASM integration is working correctly.');
        
    } catch (error) {
        console.error('ERROR during testing:', error.message);
        process.exit(1);
    }
}

// Run the test
testPerfettoWasm().catch(error => {
    console.error('Test failed:', error);
    process.exit(1);
});