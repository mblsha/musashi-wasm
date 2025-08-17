#!/usr/bin/env node

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Ensure directories exist
const libDir = path.join(__dirname, '..', 'lib');
const distDir = path.join(__dirname, '..', 'dist');

if (!fs.existsSync(libDir)) {
  fs.mkdirSync(libDir, { recursive: true });
}
if (!fs.existsSync(distDir)) {
  fs.mkdirSync(distDir, { recursive: true });
}

// Generate CommonJS wrapper for standard build (kept for completeness; not exported)
const cjsWrapper = `const fs = require('fs');
const path = require('path');

let Module = null;
let modulePromise = null;

function loadMusashi() {
  if (modulePromise) return modulePromise;
  
  modulePromise = new Promise(async (resolve, reject) => {
    try {
      const wasmPath = path.join(__dirname, '..', 'dist', 'musashi.wasm');
      const wasmBuffer = fs.readFileSync(wasmPath);
      
      // Create module configuration
      const moduleConfig = {
        wasmBinary: wasmBuffer,
        onRuntimeInitialized: function() {
          Module = this;
          resolve(this);
        }
      };
      
      // Load the emscripten-generated module
      const mod = require('../dist/musashi-loader.cjs');
      const EmscriptenModule = (mod && (mod.default || mod));
      EmscriptenModule(moduleConfig);
    } catch (error) {
      reject(error);
    }
  });
  
  return modulePromise;
}

class Musashi {
  constructor() {
    this.module = null;
    this.initialized = false;
  }
  
  async init() {
    if (this.initialized) return;
    this.module = await loadMusashi();
    this.module._m68k_init();
    this.initialized = true;
  }
  
  // CPU Control
  execute(cycles) {
    return this.module._m68k_execute(cycles);
  }
  
  pulseReset() {
    this.module._m68k_pulse_reset();
  }
  
  cyclesRun() {
    return this.module._m68k_cycles_run();
  }
  
  // Register Access
  getReg(reg) {
    return this.module._m68k_get_reg(null, reg);
  }
  
  setReg(reg, value) {
    this.module._m68k_set_reg(reg, value);
  }
  
  // Memory Management
  addRegion(base, size, dataPtr) {
    return this.module._add_region(base, size, dataPtr);
  }
  
  clearRegions() {
    this.module._clear_regions();
  }
  
  // Callbacks
  setReadMemFunc(func) {
    const ptr = this.module.addFunction(func, 'ii');
    this.module._set_read_mem_func(ptr);
    return ptr;
  }
  
  setWriteMemFunc(func) {
    const ptr = this.module.addFunction(func, 'vii');
    this.module._set_write_mem_func(ptr);
    return ptr;
  }
  
  setPCHookFunc(func) {
    const ptr = this.module.addFunction(func, 'ii');
    this.module._set_pc_hook_func(ptr);
    return ptr;
  }
  
  setFullInstrHookFunc(func) {
    const ptr = this.module.addFunction(func, 'ii');
    this.module._set_full_instr_hook_func(ptr);
    return ptr;
  }
  
  removeFunction(ptr) {
    this.module.removeFunction(ptr);
  }
  
  // Memory Access
  allocateMemory(size) {
    return this.module._malloc(size);
  }
  
  freeMemory(ptr) {
    this.module._free(ptr);
  }
  
  writeMemory(ptr, data) {
    this.module.HEAPU8.set(data, ptr);
  }
  
  readMemory(ptr, size) {
    return new Uint8Array(this.module.HEAPU8.buffer, ptr, size);
  }
  
  // Debug
  enablePrintfLogging(enable) {
    this.module._enable_printf_logging(enable ? 1 : 0);
  }
}

module.exports = Musashi;
module.exports.Musashi = Musashi;
module.exports.default = Musashi;
`;

// Generate ESM wrapper for standard build
const esmWrapper = `import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

let Module = null;
let modulePromise = null;

function loadMusashi() {
  if (modulePromise) return modulePromise;
  
  modulePromise = new Promise(async (resolve, reject) => {
    try {
      const wasmPath = path.join(__dirname, '..', 'dist', 'musashi.wasm');
      const wasmBuffer = fs.readFileSync(wasmPath);
      
      // Dynamically import the ESM emscripten-generated module
      const { default: EmscriptenModule } = await import('../dist/musashi-loader.mjs');
      const moduleConfig = {
        wasmBinary: wasmBuffer,
        onRuntimeInitialized: function() {
          Module = this;
          resolve(this);
        }
      };
      EmscriptenModule(moduleConfig);
    } catch (error) {
      reject(error);
    }
  });
  
  return modulePromise;
}

export class Musashi {
  constructor() {
    this.module = null;
    this.initialized = false;
  }
  
  async init() {
    if (this.initialized) return;
    this.module = await loadMusashi();
    this.module._m68k_init();
    this.initialized = true;
  }
  
  // CPU Control
  execute(cycles) {
    return this.module._m68k_execute(cycles);
  }
  
  pulseReset() {
    this.module._m68k_pulse_reset();
  }
  
  cyclesRun() {
    return this.module._m68k_cycles_run();
  }
  
  // Register Access
  getReg(reg) {
    return this.module._m68k_get_reg(null, reg);
  }
  
  setReg(reg, value) {
    this.module._m68k_set_reg(reg, value);
  }
  
  // Memory Management
  addRegion(base, size, dataPtr) {
    return this.module._add_region(base, size, dataPtr);
  }
  
  clearRegions() {
    this.module._clear_regions();
  }
  
  // Callbacks
  setReadMemFunc(func) {
    const ptr = this.module.addFunction(func, 'ii');
    this.module._set_read_mem_func(ptr);
    return ptr;
  }
  
  setWriteMemFunc(func) {
    const ptr = this.module.addFunction(func, 'vii');
    this.module._set_write_mem_func(ptr);
    return ptr;
  }
  
  setPCHookFunc(func) {
    const ptr = this.module.addFunction(func, 'ii');
    this.module._set_pc_hook_func(ptr);
    return ptr;
  }
  
  setFullInstrHookFunc(func) {
    const ptr = this.module.addFunction(func, 'ii');
    this.module._set_full_instr_hook_func(ptr);
    return ptr;
  }
  
  removeFunction(ptr) {
    this.module.removeFunction(ptr);
  }
  
  // Memory Access
  allocateMemory(size) {
    return this.module._malloc(size);
  }
  
  freeMemory(ptr) {
    this.module._free(ptr);
  }
  
  writeMemory(ptr, data) {
    this.module.HEAPU8.set(data, ptr);
  }
  
  readMemory(ptr, size) {
    return new Uint8Array(this.module.HEAPU8.buffer, ptr, size);
  }
  
  // Debug
  enablePrintfLogging(enable) {
    this.module._enable_printf_logging(enable ? 1 : 0);
  }
}

export default Musashi;
`;

// Generate Perfetto-enabled CommonJS wrapper
const perfettoCjsWrapper = `const fs = require('fs');
const path = require('path');

let Module = null;
let modulePromise = null;

function loadMusashiPerfetto() {
  if (modulePromise) return modulePromise;
  
  modulePromise = new Promise((resolve, reject) => {
    try {
      const wasmPath = path.join(__dirname, '..', 'dist', 'musashi-perfetto.wasm');
      const wasmBuffer = fs.readFileSync(wasmPath);
      
      const moduleConfig = {
        wasmBinary: wasmBuffer,
        onRuntimeInitialized: function() {
          Module = this;
          resolve(this);
        }
      };
      
      const EmscriptenModule = require('../dist/musashi-perfetto-loader.cjs');
      EmscriptenModule(moduleConfig);
    } catch (error) {
      reject(error);
    }
  });
  
  return modulePromise;
}

class MusashiPerfetto {
  constructor() {
    this.module = null;
    this.initialized = false;
  }
  
  async init(processName = 'Musashi') {
    if (this.initialized) return;
    this.module = await loadMusashiPerfetto();
    this.module._m68k_init();
    
    // Initialize Perfetto
    const namePtr = this.module.allocateUTF8(processName);
    this.module._m68k_perfetto_init(namePtr);
    this.module._free(namePtr);
    
    this.initialized = true;
  }
  
  // All standard Musashi methods...
  execute(cycles) {
    return this.module._m68k_execute(cycles);
  }
  
  pulseReset() {
    this.module._m68k_pulse_reset();
  }
  
  cyclesRun() {
    return this.module._m68k_cycles_run();
  }
  
  getReg(reg) {
    return this.module._m68k_get_reg(null, reg);
  }
  
  setReg(reg, value) {
    this.module._m68k_set_reg(reg, value);
  }
  
  addRegion(base, size, dataPtr) {
    return this.module._add_region(base, size, dataPtr);
  }
  
  clearRegions() {
    this.module._clear_regions();
  }
  
  setReadMemFunc(func) {
    const ptr = this.module.addFunction(func, 'ii');
    this.module._set_read_mem_func(ptr);
    return ptr;
  }
  
  setWriteMemFunc(func) {
    const ptr = this.module.addFunction(func, 'vii');
    this.module._set_write_mem_func(ptr);
    return ptr;
  }
  
  setPCHookFunc(func) {
    const ptr = this.module.addFunction(func, 'ii');
    this.module._set_pc_hook_func(ptr);
    return ptr;
  }
  
  setFullInstrHookFunc(func) {
    const ptr = this.module.addFunction(func, 'ii');
    this.module._set_full_instr_hook_func(ptr);
    return ptr;
  }
  
  removeFunction(ptr) {
    this.module.removeFunction(ptr);
  }
  
  allocateMemory(size) {
    return this.module._malloc(size);
  }
  
  freeMemory(ptr) {
    this.module._free(ptr);
  }
  
  writeMemory(ptr, data) {
    this.module.HEAPU8.set(data, ptr);
  }
  
  readMemory(ptr, size) {
    return new Uint8Array(this.module.HEAPU8.buffer, ptr, size);
  }
  
  enablePrintfLogging(enable) {
    this.module._enable_printf_logging(enable ? 1 : 0);
  }
  
  // Perfetto-specific methods
  enableFlowTracing(enable) {
    this.module._m68k_perfetto_enable_flow(enable ? 1 : 0);
  }
  
  enableInstructionTracing(enable) {
    this.module._m68k_perfetto_enable_instructions(enable ? 1 : 0);
  }
  
  enableMemoryTracing(enable) {
    this.module._m68k_perfetto_enable_memory(enable ? 1 : 0);
  }
  
  enableInterruptTracing(enable) {
    this.module._m68k_perfetto_enable_interrupts(enable ? 1 : 0);
  }
  
  async exportTrace() {
    const dataPtrPtr = this.module._malloc(4);
    const sizePtr = this.module._malloc(4);
    
    try {
      this.module._m68k_perfetto_export_trace(dataPtrPtr, sizePtr);
      
      const dataPtr = this.module.getValue(dataPtrPtr, '*');
      const dataSize = this.module.getValue(sizePtr, 'i32');
      
      if (dataPtr === 0 || dataSize === 0) {
        return null;
      }
      
      // Copy trace data from WASM heap
      const traceData = new Uint8Array(dataSize);
      traceData.set(new Uint8Array(this.module.HEAPU8.buffer, dataPtr, dataSize));
      
      // Free the trace data in WASM
      this.module._m68k_perfetto_free_trace_data(dataPtr);
      
      return traceData;
    } finally {
      this.module._free(dataPtrPtr);
      this.module._free(sizePtr);
    }
  }
  
  saveTrace(filename) {
    const traceData = this.exportTrace();
    if (traceData) {
      fs.writeFileSync(filename, traceData);
      return true;
    }
    return false;
  }
}

module.exports = MusashiPerfetto;
module.exports.MusashiPerfetto = MusashiPerfetto;
module.exports.default = MusashiPerfetto;
`;

// Ensure expected Emscripten loader+wasm are present under dist/
const rootDir = path.join(__dirname, '..');
const altRootDir = path.join(__dirname, '..', '..');
const nodeJsCandidates = [
  path.join(rootDir, 'musashi-node.out.mjs'),
  path.join(altRootDir, 'musashi-node.out.mjs')
];
const nodeWasmCandidates = [
  path.join(rootDir, 'musashi-node.out.wasm'),
  path.join(altRootDir, 'musashi-node.out.wasm')
];
const loaderOut = path.join(distDir, 'musashi-loader.mjs');
const wasmOut = path.join(distDir, 'musashi.wasm');

const nodeJsIn = nodeJsCandidates.find(p => fs.existsSync(p));
const nodeWasmIn = nodeWasmCandidates.find(p => fs.existsSync(p));
if (nodeJsIn) {
  fs.copyFileSync(nodeJsIn, loaderOut);
}
if (nodeWasmIn) {
  fs.copyFileSync(nodeWasmIn, wasmOut);
}

// Write wrapper files (ESM-only)
fs.writeFileSync(path.join(libDir, 'index.mjs'), esmWrapper);

// Generate a similar ESM version for perfetto
const perfettoEsmWrapper = perfettoCjsWrapper
  .replace(/const fs = require\('fs'\);/, "import fs from 'fs';")
  .replace(/const path = require\('path'\);/, "import path from 'path';\\nimport { fileURLToPath } from 'url';\\n\\nconst __filename = fileURLToPath(import.meta.url);\\nconst __dirname = path.dirname(__filename);")
  .replace(/const EmscriptenModule = require/, "const { default: EmscriptenModule } = await import")
  .replace(/module\.exports.*$/gm, '')
  .replace(/class MusashiPerfetto/, 'export class MusashiPerfetto')
  + '\\nexport default MusashiPerfetto;';

fs.writeFileSync(path.join(libDir, 'perfetto.mjs'), perfettoEsmWrapper);

console.log('✅ Generated wrapper modules in lib/');
