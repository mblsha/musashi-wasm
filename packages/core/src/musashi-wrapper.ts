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
  removeFunction(f: EmscriptenFunction): void;
  
  // Heap access
  HEAPU8: Uint8Array;
  HEAP32: Int32Array;

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
  
  // New callback system
  _set_read8_callback?(f: EmscriptenFunction): void;
  _set_write8_callback?(f: EmscriptenFunction): void;
  _set_probe_callback?(f: EmscriptenFunction): void;
  
  // New register helpers
  _set_d_reg?(n: number, value: number): void;
  _get_d_reg?(n: number): number;
  _set_a_reg?(n: number, value: number): void;
  _get_a_reg?(n: number): number;
  _set_pc_reg?(value: number): void;
  _get_pc_reg?(): number;
  _set_sr_reg?(value: number): void;
  _get_sr_reg?(): number;
  _set_isp_reg?(value: number): void;
  _set_usp_reg?(value: number): void;
  _get_sp_reg?(): number;
  
  // Heap access for memory setup
  setValue?(ptr: number, value: number, type: string): void;
  getValue?(ptr: number, type: string): number;
}

export class MusashiWrapper {
  private _module: MusashiEmscriptenModule;
  private _system: any; // Reference to SystemImpl
  private _memory: Uint8Array = new Uint8Array(2 * 1024 * 1024); // 2MB memory
  private _readFunc: EmscriptenFunction = 0;
  private _writeFunc: EmscriptenFunction = 0;
  private _probeFunc: EmscriptenFunction = 0;
  private readonly NOP_FUNC_ADDR = 0x1000; // Address with an RTS instruction (moved away from test program)
  private _doneExec = false;
  private _doneOverride = false;

  constructor(module: MusashiEmscriptenModule) {
    this._module = module;
  }
  
  init(system: any, rom: Uint8Array, ram: Uint8Array) {
    this._system = system;
    
    // Setup callbacks FIRST (critical for expert's fix)
    this._readFunc = this._module.addFunction((addr: number) => {
      return this._memory[addr] || 0;
    }, 'ii');
    
    this._writeFunc = this._module.addFunction((addr: number, val: number) => {
      this._memory[addr] = val & 0xFF;
    }, 'vii');
    
    this._probeFunc = this._module.addFunction((addr: number) => {
      return this._system._handlePCHook(addr) ? 1 : 0;
    }, 'ii');
    
    // Register callbacks with C
    if (this._module._set_read8_callback) {
      this._module._set_read8_callback(this._readFunc);
    }
    if (this._module._set_write8_callback) {
      this._module._set_write8_callback(this._writeFunc);
    }
    if (this._module._set_probe_callback) {
      this._module._set_probe_callback(this._probeFunc);
    }
    
    // Copy ROM and RAM into our memory
    this._memory.set(rom, 0x000000);
    this._memory.set(ram, 0x100000);
    
    
    // CRITICAL: Write reset vectors BEFORE init/reset
    this.write32BE(0x00000000, 0x00108000); // Initial SSP (in RAM)
    this.write32BE(0x00000004, 0x00000400); // Initial PC (in ROM)
    
    // Write NOP function at NOP_FUNC_ADDR for override handling  
    const nopCode = new Uint8Array([0x4E, 0x75]); // RTS instruction
    this._memory.set(nopCode, this.NOP_FUNC_ADDR);
    
    // Initialize CPU
    this._module._m68k_init();
    this._module._m68k_set_context?.(0);
    
    // Reset CPU (will read vectors we just wrote)
    this._module._m68k_pulse_reset();
    
    // Verify initialization
    const pc = this._module._get_pc_reg?.() ?? this._module._m68k_get_reg(0, 16);
    if (pc !== 0x400) {
      throw new Error(`CPU not properly initialized, PC=0x${pc.toString(16)} (expected 0x400)`);
    }
  }
  
  private write32BE(addr: number, value: number): void {
    this._memory[addr + 0] = (value >>> 24) & 0xFF;
    this._memory[addr + 1] = (value >>> 16) & 0xFF;
    this._memory[addr + 2] = (value >>> 8) & 0xFF;
    this._memory[addr + 3] = value & 0xFF;
  }

  cleanup() {
    // Clean up function pointers
    if (this._readFunc) {
      this._module.removeFunction?.(this._readFunc);
      this._readFunc = 0;
    }
    if (this._writeFunc) {
      this._module.removeFunction?.(this._writeFunc);
      this._writeFunc = 0;
    }
    if (this._probeFunc) {
      this._module.removeFunction?.(this._probeFunc);
      this._probeFunc = 0;
    }
    this._module._clear_regions?.();
    this._module._clear_pc_hook_addrs?.();
    this._module._clear_pc_hook_func?.();
    this._module._reset_myfunc_state?.();
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
    // Use new register helpers when available
    if (index >= 0 && index <= 7 && this._module._get_d_reg) {
      return this._module._get_d_reg(index) >>> 0; // Ensure unsigned
    } else if (index >= 8 && index <= 14 && this._module._get_a_reg) {
      return this._module._get_a_reg(index - 8) >>> 0; // Ensure unsigned
    } else if (index === 15 && this._module._get_sp_reg) {
      return this._module._get_sp_reg() >>> 0; // Ensure unsigned
    } else if (index === 16 && this._module._get_pc_reg) {
      return this._module._get_pc_reg() >>> 0; // Ensure unsigned
    } else if (index === 17 && this._module._get_sr_reg) {
      return this._module._get_sr_reg() >>> 0; // Ensure unsigned
    }
    
    // Fallback to old system
    return this._module._m68k_get_reg(0, index) >>> 0; // Ensure unsigned
  }
  
  set_reg(index: number, value: number) { 
    // Use new register helpers when available
    if (index >= 0 && index <= 7 && this._module._set_d_reg) {
      this._module._set_d_reg(index, value);
    } else if (index >= 8 && index <= 14 && this._module._set_a_reg) {
      this._module._set_a_reg(index - 8, value);
    } else if (index === 15) {
      // SP - need to check supervisor mode and set appropriate stack
      const sr = this.get_reg(17);
      if ((sr & 0x2000) !== 0 && this._module._set_isp_reg) {
        this._module._set_isp_reg(value); // Supervisor mode: set ISP
      } else if (this._module._set_usp_reg) {
        this._module._set_usp_reg(value); // User mode: set USP
      } else {
        this._module._m68k_set_reg(index, value);
      }
    } else if (index === 16 && this._module._set_pc_reg) {
      this._module._set_pc_reg(value);
    } else if (index === 17 && this._module._set_sr_reg) {
      this._module._set_sr_reg(value);
    } else {
      // Fallback to old system
      this._module._m68k_set_reg(index, value);
    }
  }
  
  add_pc_hook_addr(addr: number) { 
    this._module._add_pc_hook_addr(addr); 
  }
  
  read_memory(address: number, size: 1 | 2 | 4): number { 
    // Read from our unified memory (big-endian composition)
    if (address >= this._memory.length) return 0;
    
    if (size === 1) {
      return this._memory[address];
    } else if (size === 2) {
      return (this._memory[address] << 8) | this._memory[address + 1];
    } else {
      return (this._memory[address] << 24) | 
             (this._memory[address + 1] << 16) | 
             (this._memory[address + 2] << 8) | 
             this._memory[address + 3];
    }
  }
  
  write_memory(address: number, size: 1 | 2 | 4, value: number) { 
    // Write to our unified memory (big-endian decomposition)
    if (address >= this._memory.length) return;
    
    if (size === 1) {
      this._memory[address] = value & 0xFF;
    } else if (size === 2) {
      this._memory[address] = (value >> 8) & 0xFF;
      this._memory[address + 1] = value & 0xFF;
    } else {
      this._memory[address] = (value >> 24) & 0xFF;
      this._memory[address + 1] = (value >> 16) & 0xFF;
      this._memory[address + 2] = (value >> 8) & 0xFF;
      this._memory[address + 3] = value & 0xFF;
    }
    
    // Update the JS-side RAM copy if this is in RAM space
    const ramStart = 0x100000;
    if (address >= ramStart && address < ramStart + this._system._ram.length) {
      const offset = address - ramStart;
      if (size === 1) {
        this._system._ram[offset] = value & 0xFF;
      } else if (size === 2) {
        this._system._ram[offset] = (value >> 8) & 0xFF;
        this._system._ram[offset + 1] = value & 0xFF;
      } else {
        this._system._ram[offset] = (value >> 24) & 0xFF;
        this._system._ram[offset + 1] = (value >> 16) & 0xFF;
        this._system._ram[offset + 2] = (value >> 8) & 0xFF;
        this._system._ram[offset + 3] = value & 0xFF;
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
    // Write string to WASM heap manually
    for (let i = 0; i < processName.length; i++) {
      this._module.HEAPU8[namePtr + i] = processName.charCodeAt(i);
    }
    this._module.HEAPU8[namePtr + processName.length] = 0; // null terminator
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
    // Write string to WASM heap manually
    for (let i = 0; i < name.length; i++) {
      this._module.HEAPU8[namePtr + i] = name.charCodeAt(i);
    }
    this._module.HEAPU8[namePtr + name.length] = 0; // null terminator
    try {
      func.call(this._module, address, namePtr);
    } finally {
      this._module._free(namePtr);
    }
  }

  registerFunctionName(address: number, name: string) {
    if (this._module._register_function_name) {
      this._registerSymbol(this._module._register_function_name.bind(this._module), address, name);
    }
    // Note: ccall fallback removed as it's not typically exported
  }

  registerMemoryName(address: number, name: string) {
    if (this._module._register_memory_name) {
      this._registerSymbol(this._module._register_memory_name.bind(this._module), address, name);
    }
    // Note: ccall fallback removed as it's not typically exported
  }

  perfettoExportTrace(): Uint8Array | null {
    if (!this._module._m68k_perfetto_export_trace) return null;

    const dataPtrPtr = this._module._malloc(4);
    const sizePtr = this._module._malloc(4);
    
    try {
      const result = this._module._m68k_perfetto_export_trace(dataPtrPtr, sizePtr);
      if (result !== 0) return null;

      // Read pointer values from WASM heap
      const dataPtr = this._module.HEAP32[dataPtrPtr >> 2];
      const dataSize = this._module.HEAP32[sizePtr >> 2];

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
  let module: any;
  if (isNode) {
    // For Node.js, use the ESM wrapper
    // @ts-ignore - Dynamic import of .mjs file
    const { default: createMusashiModule } = await import('../wasm/musashi-node-wrapper.mjs');
    module = await createMusashiModule();
    
    // Runtime validation to catch shape mismatches early
    if (!module || typeof module._m68k_init !== 'function') {
      const keys = module ? Object.keys(module).slice(0, 20) : [];
      throw new Error(
        `Musashi Module shape unexpected: _m68k_init not found. ` +
        `Type=${typeof module}, keys=${JSON.stringify(keys)}`
      );
    }
  } else {
    // For browser, import the web ESM version
    // Use variable specifier to avoid TS2307 compile-time resolution
    const specifier = '../../../musashi.out.mjs';
    // @ts-ignore - Dynamic import of .mjs file
    const mod = await import(/* webpackIgnore: true */ specifier);
    const moduleFactory = mod.default;
    module = await moduleFactory();
  }
  
  return new MusashiWrapper(module as MusashiEmscriptenModule);
}