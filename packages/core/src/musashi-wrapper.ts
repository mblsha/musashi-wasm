// This is a wrapper around the Emscripten module

// These types are placeholders for Emscripten's internal types
type EmscriptenBuffer = number;
type EmscriptenFunction = number;

interface MusashiEmscriptenModule {
  _my_initialize(): boolean;
  _add_pc_hook_addr(addr: number): void;
  _add_region(start: number, len: number, buf: EmscriptenBuffer): void;
  _m68k_execute(cycles: number): number;
  _m68k_get_reg(context: number, index: number): number;
  _m68k_init(): void;
  _m68k_pulse_reset(): void;
  _m68k_set_context(context: number): void;
  _m68k_set_reg(index: number, value: number): void;
  _malloc(size: number): EmscriptenBuffer;
  _free(ptr: EmscriptenBuffer): void;
  _set_pc_hook_func(f: EmscriptenFunction): void;
  _set_read_mem_func(f: EmscriptenFunction): void;
  _set_write_mem_func(f: EmscriptenFunction): void;
  _clear_regions(): void;
  _clear_pc_hook_addrs(): void;
  _clear_pc_hook_func(): void;
  _reset_myfunc_state(): void;
  addFunction(f: unknown, type: string): EmscriptenFunction;
  getValue(ptr: EmscriptenBuffer, type: string): number;
  setValue(ptr: EmscriptenBuffer, value: number, type: string): void;
  writeArrayToMemory(array: Uint8Array, ptr: EmscriptenBuffer): void;
  stringToUTF8(str: string, outPtr: number, maxBytesToWrite: number): number;
  HEAPU8: Uint8Array;
  ccall(ident: string, returnType: string | null, argTypes: string[], args: any[]): any;

  // Optional Perfetto functions
  _m68k_perfetto_init?(process_name: EmscriptenBuffer): number;
  _m68k_perfetto_destroy?(): void;
  _m68k_perfetto_cleanup_slices?(): void;
  _m68k_perfetto_enable_flow?(enable: number): void;
  _m68k_perfetto_enable_memory?(enable: number): void;
  _m68k_perfetto_enable_instructions?(enable: number): void;
  _m68k_perfetto_export_trace?(data_out: EmscriptenBuffer, size_out: EmscriptenBuffer): number;
  _m68k_perfetto_free_trace_data?(data: EmscriptenBuffer): void;
  _m68k_perfetto_is_initialized?(): number;
  _m68k_trace_enable?(enable: number): void;
  _register_function_name?(address: number, name: EmscriptenBuffer): void;
  _register_memory_name?(address: number, name: EmscriptenBuffer): void;
}

export class MusashiWrapper {
  private _module: MusashiEmscriptenModule;
  private _system: any; // Reference to SystemImpl
  private _romBuffer: EmscriptenBuffer = 0;
  private _ramBuffer: EmscriptenBuffer = 0;
  private _readFunc: EmscriptenFunction = 0;
  private _writeFunc: EmscriptenFunction = 0;
  private _pcHookFunc: EmscriptenFunction = 0;
  private readonly NOP_FUNC_ADDR = 0x400; // Address with an RTS instruction
  private _doneExec = false;
  private _doneOverride = false;

  constructor(module: MusashiEmscriptenModule) {
    this._module = module;
  }
  
  init(system: any, rom: Uint8Array, ram: Uint8Array) {
    this._system = system;
    
    // Initialize musashi
    this._module._m68k_init();
    this._module._m68k_set_context(0);

    // Setup ROM region
    this._romBuffer = this._module._malloc(rom.length);
    this._module.writeArrayToMemory(rom, this._romBuffer);
    this._module._add_region(0x000000, rom.length, this._romBuffer);
    
    // Setup RAM region
    this._ramBuffer = this._module._malloc(ram.length);
    this._module.writeArrayToMemory(ram, this._ramBuffer);
    this._module._add_region(0x100000, ram.length, this._ramBuffer);
    
    // Setup memory handlers
    this._readFunc = this._module.addFunction(this.readHandler.bind(this), 'iii');
    this._writeFunc = this._module.addFunction(this.writeHandler.bind(this), 'viii');
    this._pcHookFunc = this._module.addFunction(this.pcHookHandler.bind(this), 'ii');
    
    this._module._set_read_mem_func(this._readFunc);
    this._module._set_write_mem_func(this._writeFunc);
    this._module._set_pc_hook_func(this._pcHookFunc);

    // Write NOP function at NOP_FUNC_ADDR for override handling
    const nopCode = new Uint8Array([0x4E, 0x75]); // RTS instruction
    for (let i = 0; i < nopCode.length; i++) {
      this._module.HEAPU8[this._romBuffer + this.NOP_FUNC_ADDR + i] = nopCode[i];
    }

    this.pulse_reset();
  }

  cleanup() {
    if (this._romBuffer) {
      this._module._free(this._romBuffer);
      this._romBuffer = 0;
    }
    if (this._ramBuffer) {
      this._module._free(this._ramBuffer);
      this._ramBuffer = 0;
    }
    this._module._clear_regions();
    this._module._clear_pc_hook_addrs();
    this._module._clear_pc_hook_func();
    this._module._reset_myfunc_state();
  }

  readHandler(address: number, size: 1 | 2 | 4): number {
    // The regions should handle most reads, but this is a fallback
    return this._system.read(address, size);
  }
  
  writeHandler(address: number, size: 1 | 2 | 4, value: number): void {
    // The regions should handle most writes, but this is a fallback
    this._system.write(address, size, value);
  }

  pcHookHandler(pc: number): number {
    // Check for RTS detection for call() method
    const sr = this.get_reg(17); // Status register
    const currentSP = this.get_reg(15); // Stack pointer
    
    // Simple RTS detection (this is a simplified version)
    if (pc === this.NOP_FUNC_ADDR) {
      this._doneExec = true;
      return 1; // Stop execution
    }

    // Let the SystemImpl class handle the logic
    const shouldStop = this._system._handlePCHook(pc);
    
    if (shouldStop) {
      this._doneOverride = true;
      return 1; // Stop execution
    }
    
    return 0; // Continue execution
  }

  execute(cycles: number): number {
    return this._module._m68k_execute(cycles);
  }

  call(address: number): number {
    this._doneExec = false;
    this._doneOverride = false;
    this.set_reg(16, address); // Set PC
    let cycles = 0;
    while (!this._doneExec) {
        cycles += this._module._m68k_execute(1_000_000);
        if (this._doneOverride) {
            this._doneOverride = false;
            this.set_reg(16, this.NOP_FUNC_ADDR);
        }
    }
    return cycles;
  }

  pulse_reset() { 
    this._module._m68k_pulse_reset(); 
  }
  
  get_reg(index: number): number { 
    return this._module._m68k_get_reg(0, index); 
  }
  
  set_reg(index: number, value: number) { 
    this._module._m68k_set_reg(index, value); 
  }
  
  add_pc_hook_addr(addr: number) { 
    this._module._add_pc_hook_addr(addr); 
  }
  
  read_memory(address: number, size: 1 | 2 | 4): number { 
    // Direct memory access through WASM heap
    const ramStart = 0x100000;
    const ramSize = this._system._ram.length;
    
    if (address >= ramStart && address < ramStart + ramSize) {
      const offset = address - ramStart;
      const ramPtr = this._ramBuffer + offset;
      if (size === 1) return this._module.HEAPU8[ramPtr];
      if (size === 2) return (this._module.HEAPU8[ramPtr] << 8) | this._module.HEAPU8[ramPtr + 1];
      return (this._module.HEAPU8[ramPtr] << 24) | 
             (this._module.HEAPU8[ramPtr + 1] << 16) | 
             (this._module.HEAPU8[ramPtr + 2] << 8) | 
             this._module.HEAPU8[ramPtr + 3];
    }
    
    // ROM access
    if (address < this._system._rom.length) {
      const romPtr = this._romBuffer + address;
      if (size === 1) return this._module.HEAPU8[romPtr];
      if (size === 2) return (this._module.HEAPU8[romPtr] << 8) | this._module.HEAPU8[romPtr + 1];
      return (this._module.HEAPU8[romPtr] << 24) | 
             (this._module.HEAPU8[romPtr + 1] << 16) | 
             (this._module.HEAPU8[romPtr + 2] << 8) | 
             this._module.HEAPU8[romPtr + 3];
    }
    
    return 0;
  }
  
  write_memory(address: number, size: 1 | 2 | 4, value: number) { 
    const ramStart = 0x100000;
    const ramSize = this._system._ram.length;
    
    if (address >= ramStart && address < ramStart + ramSize) {
      const offset = address - ramStart;
      const ramPtr = this._ramBuffer + offset;
      if (size === 1) {
        this._module.HEAPU8[ramPtr] = value & 0xff;
      } else if (size === 2) {
        this._module.HEAPU8[ramPtr] = (value >> 8) & 0xff;
        this._module.HEAPU8[ramPtr + 1] = value & 0xff;
      } else {
        this._module.HEAPU8[ramPtr] = (value >> 24) & 0xff;
        this._module.HEAPU8[ramPtr + 1] = (value >> 16) & 0xff;
        this._module.HEAPU8[ramPtr + 2] = (value >> 8) & 0xff;
        this._module.HEAPU8[ramPtr + 3] = value & 0xff;
      }
    }
  }

  // --- Perfetto Tracing Wrappers ---
  isPerfettoAvailable(): boolean {
    return typeof this._module._m68k_perfetto_init === 'function';
  }
  
  perfettoInit(processName: string): number {
    if (!this._module._m68k_perfetto_init) return -1;
    const namePtr = this._module._malloc(processName.length + 1);
    this._module.stringToUTF8(processName, namePtr, processName.length + 1);
    try {
      return this._module._m68k_perfetto_init(namePtr);
    } finally {
      this._module._free(namePtr);
    }
  }

  perfettoDestroy() { 
    this._module._m68k_perfetto_destroy?.(); 
  }
  
  perfettoCleanupSlices() { 
    this._module._m68k_perfetto_cleanup_slices?.(); 
  }
  
  perfettoEnableFlow(enable: boolean) { 
    this._module._m68k_perfetto_enable_flow?.(enable ? 1 : 0); 
  }
  
  perfettoEnableMemory(enable: boolean) { 
    this._module._m68k_perfetto_enable_memory?.(enable ? 1 : 0); 
  }
  
  perfettoEnableInstructions(enable: boolean) { 
    this._module._m68k_perfetto_enable_instructions?.(enable ? 1 : 0); 
  }
  
  traceEnable(enable: boolean) { 
    this._module._m68k_trace_enable?.(enable ? 1 : 0); 
  }

  perfettoIsInitialized(): boolean {
    if (!this._module._m68k_perfetto_is_initialized) return false;
    return this._module._m68k_perfetto_is_initialized() !== 0;
  }

  private _registerSymbol(func: (address: number, name: EmscriptenBuffer) => void, address: number, name: string) {
    const namePtr = this._module._malloc(name.length + 1);
    this._module.stringToUTF8(name, namePtr, name.length + 1);
    try {
      func.call(this._module, address, namePtr);
    } finally {
      this._module._free(namePtr);
    }
  }

  registerFunctionName(address: number, name: string) {
    if (this._module._register_function_name) {
      this._registerSymbol(this._module._register_function_name.bind(this._module), address, name);
    } else if (this._module.ccall) {
      this._module.ccall('register_function_name', null, ['number', 'string'], [address, name]);
    }
  }

  registerMemoryName(address: number, name: string) {
    if (this._module._register_memory_name) {
      this._registerSymbol(this._module._register_memory_name.bind(this._module), address, name);
    } else if (this._module.ccall) {
      this._module.ccall('register_memory_name', null, ['number', 'string'], [address, name]);
    }
  }

  perfettoExportTrace(): Uint8Array | null {
    if (!this._module._m68k_perfetto_export_trace) return null;

    const dataPtrPtr = this._module._malloc(4);
    const sizePtr = this._module._malloc(4);
    
    try {
      const result = this._module._m68k_perfetto_export_trace(dataPtrPtr, sizePtr);
      if (result !== 0) return null;

      const dataPtr = this._module.getValue(dataPtrPtr, 'i32');
      const dataSize = this._module.getValue(sizePtr, 'i32');

      if (dataPtr === 0 || dataSize === 0) {
        return new Uint8Array(0);
      }

      const traceData = new Uint8Array(this._module.HEAPU8.subarray(dataPtr, dataPtr + dataSize));
      this._module._m68k_perfetto_free_trace_data!(dataPtr);
      return traceData;
    } finally {
      this._module._free(dataPtrPtr);
      this._module._free(sizePtr);
    }
  }
}

// Factory function to load and initialize the wasm module
export async function getModule(): Promise<MusashiWrapper> {
  // Environment detection without DOM types
  const isNode =
    typeof globalThis !== 'undefined' &&
    !!(globalThis as any).process?.versions?.node;
  
  // Dynamic import based on environment
  let moduleFactory: any;
  if (isNode) {
    // For Node.js, we need to use the wrapper
    moduleFactory = require('../wasm/musashi-node-wrapper.js');
  } else {
    // For browser, import the web version
    // Use variable specifier to avoid TS2307 compile-time resolution
    const specifier = '../../../musashi.out.js';
    const mod = await import(/* webpackIgnore: true */ specifier);
    moduleFactory = mod.default;
  }
  
  const module = await moduleFactory();
  return new MusashiWrapper(module as MusashiEmscriptenModule);
}