#!/usr/bin/env node

/**
 * Test script to verify Perfetto-enabled WASM build functionality
 * This script tests that the Perfetto tracing functions are properly exported
 * and can be called without errors.
 * 
 * ESM module format for Node.js
 */

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

// Get __dirname equivalent in ESM
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Import the Node.js version of the WASM module
const musashiPath = path.join(__dirname, 'musashi-node.out.mjs');

if (!fs.existsSync(musashiPath)) {
    console.error('ERROR: musashi-node.out.mjs not found. Build the WASM module first.');
    process.exit(1);
}

console.log('Loading Musashi WASM module with Perfetto support...');
console.log(`Module path: ${musashiPath}`);

// Dynamic import for the ESM module
const loadedModule = await import(musashiPath);

async function testPerfettoWasm() {
    try {
        let Module;
        
        // Determine module format and load appropriately
        console.log('Detecting module format...');
        console.log('Type of loaded module:', typeof loadedModule);
        console.log('Has default?', typeof loadedModule.default === 'function');
        console.log('Has createMusashi?', typeof loadedModule.createMusashi === 'function');
        
        if (typeof loadedModule.default === 'function') {
            // ESM default export (most likely with EXPORT_ES6=1)
            console.log('Module format: ESM default export');
            Module = await loadedModule.default();
        } else if (typeof loadedModule.createMusashi === 'function') {
            // Named export
            console.log('Module format: Named export (createMusashi)');
            Module = await loadedModule.createMusashi();
        } else if (typeof loadedModule === 'function') {
            // Module itself is a factory function
            console.log('Module format: Factory function');
            Module = await loadedModule();
        } else {
            // Unknown format - list what we see
            console.error('Unknown module format. Available properties:');
            const props = Object.keys(loadedModule).filter(k => !k.startsWith('_'));
            console.error('Non-underscore properties:', props.slice(0, 10));
            const funcs = Object.keys(loadedModule).filter(k => typeof loadedModule[k] === 'function');
            console.error('Functions:', funcs.slice(0, 10));
            throw new Error('Could not determine module format');
        }
        
        console.log('✓ WASM module loaded successfully');
        console.log('Module type after loading:', typeof Module);
        console.log('Module has _m68k_init?', typeof Module._m68k_init === 'function');
        
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