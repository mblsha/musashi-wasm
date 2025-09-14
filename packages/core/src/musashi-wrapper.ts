// This is a wrapper around the Emscripten module

// These types are placeholders for Emscripten's internal types
type EmscriptenBuffer = number;
type EmscriptenFunction = number;
import { M68kRegister } from '@m68k/common';

export interface MusashiEmscriptenModule {
  _my_initialize(): boolean;
  _add_pc_hook_addr(addr: number): void;
  _add_region(start: number, len: number, buf: EmscriptenBuffer): void;
  _m68k_execute(cycles: number): number;
  _m68k_step_one(): number;
  _m68k_get_reg(context: number, index: number): number;
  _m68k_init(): void;
  _m68k_pulse_reset(): void;
  _m68k_set_context(context: number): void;
  _m68k_set_reg(index: number, value: number): void;
  _malloc(size: number): EmscriptenBuffer;
  _free(ptr: EmscriptenBuffer): void;
  _set_read_mem_func(f: EmscriptenFunction): void;
  _set_write_mem_func(f: EmscriptenFunction): void;
  _set_pc_hook_func(f: EmscriptenFunction): void;
  _clear_regions(): void;
  _clear_pc_hook_addrs(): void;
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
  _m68k_perfetto_export_trace?(
    data_out: EmscriptenBuffer,
    size_out: EmscriptenBuffer
  ): number;
  _m68k_perfetto_free_trace_data?(data: EmscriptenBuffer): void;
  _m68k_perfetto_is_initialized?(): number;
  _m68k_trace_enable?(enable: number): void;
  _register_function_name?(address: number, name: EmscriptenBuffer): void;
  _register_memory_name?(address: number, name: EmscriptenBuffer): void;

  // Raw m68ktrace memory callback + toggles
  _m68k_set_trace_mem_callback?(f: EmscriptenFunction): void;
  _m68k_trace_set_mem_enabled?(enable: number): void;
  _m68k_trace_add_mem_region?(start: number, end: number): number;
  _m68k_trace_clear_mem_regions?(): void;

  // New callback system
  // callbacks defined above
  // Disassembler entry point (optional export)
  _m68k_disassemble?(
    outBuf: EmscriptenBuffer,
    pc: number,
    cpu_type: number
  ): number;

  
  // New C++-side session helper
  _m68k_call_until_js_stop?(entry_pc: number, timeslice: number): number;
  // Test/debug helpers
  _m68k_get_last_break_reason?(): number;
  _m68k_reset_last_break_reason?(): void;

  // Heap access for memory setup
  setValue?(ptr: number, value: number, type: string): void;
  getValue?(ptr: number, type: string): number;
}

interface SystemBridge {
  read(address: number, size: 1 | 2 | 4): number;
  write(address: number, size: 1 | 2 | 4, value: number): void;
  _handlePCHook(pc: number): boolean;
  ram: Uint8Array;
  // Memory trace dispatchers supplied by SystemImpl
  _handleMemoryRead?(addr: number, size: 1 | 2 | 4, value: number, pc: number): void;
  _handleMemoryWrite?(addr: number, size: 1 | 2 | 4, value: number, pc: number): void;
}

export class MusashiWrapper {
  private _module: MusashiEmscriptenModule;
  private _system!: SystemBridge; // Reference to SystemImpl
  private _memory: Uint8Array = new Uint8Array(2 * 1024 * 1024); // 2MB memory
  private _readFunc: EmscriptenFunction = 0;
  private _writeFunc: EmscriptenFunction = 0;
  private _probeFunc: EmscriptenFunction = 0;
  private _memTraceFunc: EmscriptenFunction = 0;
  private _memTraceActive = false;
  // No JS sentinel state; C++ session owns sentinel behavior.
  // CPU type for disassembler (68000)
  private readonly CPU_68000 = 0;

  constructor(module: MusashiEmscriptenModule) {
    this._module = module;
  }

  init(system: SystemBridge, rom: Uint8Array, ram: Uint8Array) {
    this._system = system;

    // Setup callbacks FIRST (critical for expert's fix)
    this._readFunc = this._module.addFunction((addr: number) => {
      const a = addr >>> 0;
      return (this._memory[a] || 0) & 0xff;
    }, 'ii');

    this._writeFunc = this._module.addFunction((addr: number, val: number) => {
      const a = addr >>> 0;
      this._memory[a] = val & 0xff;
    }, 'vii');

    this._probeFunc = this._module.addFunction((addr: number) => {
      return this._system._handlePCHook(addr >>> 0) ? 1 : 0;
    }, 'ii');

    // Register callbacks with C
    this._module._set_read_mem_func(this._module.addFunction((addr: number, size: number) => this.readHandler(addr >>> 0, (size as 1|2|4)), 'iii'));
    this._module._set_write_mem_func(this._module.addFunction((addr: number, size: number, val: number) => this.writeHandler(addr >>> 0, (size as 1|2|4), val >>> 0), 'viii'));
    this._module._set_pc_hook_func(this._probeFunc);

    // Copy ROM and RAM into our memory
    this._memory.set(rom, 0x000000);
    this._memory.set(ram, 0x100000);

    // CRITICAL: Write reset vectors BEFORE init/reset
    this.write32BE(0x00000000, 0x00108000); // Initial SSP (in RAM)
    this.write32BE(0x00000004, 0x00000400); // Initial PC (in ROM)

    // Initialize CPU
    this._module._m68k_init();
    this._module._m68k_set_context?.(0);

    // Reset CPU (will read vectors we just wrote)
    this._module._m68k_pulse_reset();

    // Verify initialization
    const pc = this._module._m68k_get_reg(0, M68kRegister.PC);
    if (pc !== 0x400) {
      throw new Error(
        `CPU not properly initialized, PC=0x${pc.toString(16)} (expected 0x400)`
      );
    }
  }

  private write32BE(addr: number, value: number): void {
    this._memory[addr + 0] = (value >>> 24) & 0xff;
    this._memory[addr + 1] = (value >>> 16) & 0xff;
    this._memory[addr + 2] = (value >>> 8) & 0xff;
    this._memory[addr + 3] = value & 0xff;
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
    if (this._memTraceFunc) {
      // Clear callback in core before removing to avoid dangling ptr
      this._module._m68k_set_trace_mem_callback?.(0 as unknown as number);
      this._module.removeFunction?.(this._memTraceFunc);
      this._memTraceFunc = 0;
      this._memTraceActive = false;
    }
    this._module._clear_regions?.();
    this._module._clear_pc_hook_addrs?.();
    // Disable probe callback explicitly if available
    try { this._module._set_pc_hook_func?.(0 as unknown as number); } catch {}
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
    // Delegate stop/continue decision to SystemImpl. The C++ session will
    // handle vectoring to the sentinel when we return non-zero here.
    return this._system._handlePCHook(pc) ? 1 : 0;
  }

  execute(cycles: number): number {
    return this._module._m68k_execute(cycles);
  }

  step(): number {
    return this._module._m68k_step_one();
  }

  private requireExport<K extends keyof MusashiEmscriptenModule>(name: K): void {
    const val = this._module[name];
    if (typeof val !== 'function') {
      throw new Error(`${String(name)} is not available; rebuild WASM exports`);
    }
  }

  call(address: number): number {
    // Rely on C++-side session that honors JS PC hooks and vectors to a
    // sentinel (max address) when JS requests a stop. This keeps nested
    // calls safe without opcode heuristics.
    // Ensure export exists for type safety, then cast and call
    this.requireExport('_m68k_call_until_js_stop');
    const callUntil = this._module._m68k_call_until_js_stop!;
    // Defer timeslice to C++ default by passing 0. Normalise BigInt/Number.
    const res = callUntil(address >>> 0, 0) as unknown as number | bigint;
    return Number(res) >>> 0;
  }

  // Expose break reason helpers for tests
  getLastBreakReason(): number {
    return typeof this._module._m68k_get_last_break_reason === 'function'
      ? this._module._m68k_get_last_break_reason() >>> 0
      : 0;
  }

  resetLastBreakReason(): void {
    this._module._m68k_reset_last_break_reason?.();
  }

  pulse_reset() {
    this._module._m68k_pulse_reset();
  }

  get_reg(index: M68kRegister): number {
    return this._module._m68k_get_reg(0, index) >>> 0;
  }

  set_reg(index: M68kRegister, value: number) {
    this._module._m68k_set_reg(index, value);
  }

  add_pc_hook_addr(addr: number) {
    this._module._add_pc_hook_addr(addr);
  }

  read_memory(address: number, size: 1 | 2 | 4): number {
    // Read from our unified memory (big-endian composition)
    if (address >= this._memory.length) return 0;
    if (address + size > this._memory.length) return 0;

    if (size === 1) {
      return this._memory[address];
    } else if (size === 2) {
      return (this._memory[address] << 8) | this._memory[address + 1];
    } else {
      return (
        (this._memory[address] << 24) |
        (this._memory[address + 1] << 16) |
        (this._memory[address + 2] << 8) |
        this._memory[address + 3]
      );
    }
  }

  write_memory(address: number, size: 1 | 2 | 4, value: number) {
    // Write to our unified memory (big-endian decomposition)
    if (address >= this._memory.length) return;
    if (address + size > this._memory.length) return;

    if (size === 1) {
      this._memory[address] = value & 0xff;
    } else if (size === 2) {
      this._memory[address] = (value >> 8) & 0xff;
      this._memory[address + 1] = value & 0xff;
    } else {
      this._memory[address] = (value >> 24) & 0xff;
      this._memory[address + 1] = (value >> 16) & 0xff;
      this._memory[address + 2] = (value >> 8) & 0xff;
      this._memory[address + 3] = value & 0xff;
    }

    // Update the JS-side RAM copy if this is in RAM space
    const ramStart = 0x100000;
    if (address >= ramStart && address < ramStart + this._system.ram.length) {
      const offset = address - ramStart;
      if (size === 1) {
        this._system.ram[offset] = value & 0xff;
      } else if (size === 2) {
        this._system.ram[offset] = (value >> 8) & 0xff;
        this._system.ram[offset + 1] = value & 0xff;
      } else {
        this._system.ram[offset] = (value >> 24) & 0xff;
        this._system.ram[offset + 1] = (value >> 16) & 0xff;
        this._system.ram[offset + 2] = (value >> 8) & 0xff;
        this._system.ram[offset + 3] = value & 0xff;
      }
    }
  }

  // --- Memory Trace Hook Bridge ---
  // Enable/disable forwarding of core memory trace events to SystemImpl
  setMemoryTraceEnabled(enable: boolean): void {
    if (!this._module._m68k_set_trace_mem_callback || !this._module._m68k_trace_enable || !this._module._m68k_trace_set_mem_enabled) {
      // Not supported in this build
      return;
    }
    if (enable && !this._memTraceActive) {
      if (!this._memTraceFunc) {
        // Signature from m68ktrace.h:
        // int (*m68k_trace_mem_callback)(m68k_trace_mem_type type,
        //   uint32_t pc, uint32_t address, uint32_t value, uint8_t size, uint64_t cycles);
        // Emscripten addFunction signature with WASM_BIGINT enabled: 'iiiiij'
        // (five i32 parameters followed by one i64/BigInt)
        this._memTraceFunc = this._module.addFunction(
          (
            type: number,
            pc: number,
            addr: number,
            value: number,
            size: number,
            _cycles: bigint
          ) => {
            const s = (size | 0) as 1 | 2 | 4;
            const a = addr >>> 0;
            const v = value >>> 0;
            const p = pc >>> 0;
            if (type === 0) {
              this._system._handleMemoryRead?.(a, s, v, p);
            } else {
              this._system._handleMemoryWrite?.(a, s, v, p);
            }
            return 0;
          },
          // Signature: int(type, pc, addr, value, size, cycles[i64]) -> int
          'iiiiiij'
        );
      }
      // Wire into core and turn on tracing
      this._module._m68k_set_trace_mem_callback(this._memTraceFunc);
      this._module._m68k_trace_enable(1);
      this._module._m68k_trace_set_mem_enabled(1);
      this._memTraceActive = true;
    } else if (!enable && this._memTraceActive) {
      this._module._m68k_set_trace_mem_callback(0 as unknown as number);
      this._module._m68k_trace_set_mem_enabled(0);
      this._memTraceActive = false;
    }
  }

  

  /**
   * Disassembles a single instruction at the given address.
   * Returns null if the underlying module does not expose the disassembler.
   */
  disassemble(address: number): { text: string; size: number } | null {
    const mod = this._module;
    if (typeof mod._m68k_disassemble !== 'function' || !mod._malloc || !mod._free) {
      return null;
    }
    // Allocate a small buffer for the disassembly string
    const CAP = 256;
    const buf = mod._malloc(CAP);
    try {
      const size = mod._m68k_disassemble!(buf, address >>> 0, this.CPU_68000) >>> 0;
      // Read C-string from HEAPU8 until NUL or CAP
      const heap = mod.HEAPU8 as Uint8Array;
      let s = '';
      for (let i = buf; i < buf + CAP; i++) {
        const c = heap[i];
        if (c === 0) break;
        s += String.fromCharCode(c);
      }
      return { text: s, size };
    } catch {
      return null;
    } finally {
      try {
        mod._free(buf);
      } catch {
        /* ignore */
      }
    }
  }

  /**
   * Convenience helper to disassemble a short sequence starting at address.
   */
  disassembleSequence(
    address: number,
    count: number
  ): Array<{ pc: number; text: string; size: number }> {
    const out: Array<{ pc: number; text: string; size: number }> = [];
    let pc = address >>> 0;
    for (let i = 0; i < count; i++) {
      const one = this.disassemble(pc);
      if (!one) break;
      out.push({ pc, text: one.text, size: one.size >>> 0 });
      pc = (pc + (one.size >>> 0)) >>> 0;
      if (one.size === 0) break; // avoid infinite loop on malformed decode
    }
    return out;
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

  private _registerSymbol(
    func: (address: number, name: EmscriptenBuffer) => void,
    address: number,
    name: string
  ) {
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
      this._registerSymbol(
        this._module._register_function_name.bind(this._module),
        address,
        name
      );
    }
    // Note: ccall fallback removed as it's not typically exported
  }

  registerMemoryName(address: number, name: string) {
    if (this._module._register_memory_name) {
      this._registerSymbol(
        this._module._register_memory_name.bind(this._module),
        address,
        name
      );
    }
    // Note: ccall fallback removed as it's not typically exported
  }

  perfettoExportTrace(): Uint8Array | null {
    if (!this._module._m68k_perfetto_export_trace) return null;

    const dataPtrPtr = this._module._malloc(4);
    const sizePtr = this._module._malloc(4);

    try {
      const result = this._module._m68k_perfetto_export_trace(
        dataPtrPtr,
        sizePtr
      );
      if (result !== 0) return null;

      // Read pointer values from WASM heap
      const dataPtr = this._module.HEAP32[dataPtrPtr >> 2];
      const dataSize = this._module.HEAP32[sizePtr >> 2];

      if (dataPtr === 0 || dataSize === 0) {
        return new Uint8Array(0);
      }

      const traceData = new Uint8Array(
        this._module.HEAPU8.subarray(dataPtr, dataPtr + dataSize)
      );
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
    typeof process !== 'undefined' && !!process.versions?.node;

  // Dynamic import based on environment
  let module: unknown;
  if (isNode) {
    // For Node.js, use the ESM wrapper
    // @ts-ignore - Dynamic import of .mjs file
    const { default: createMusashiModule } = await import(
      '../wasm/musashi-node-wrapper.mjs'
    );
    module = await createMusashiModule();

    // Runtime validation to catch shape mismatches early
    const modMaybe = module as Partial<MusashiEmscriptenModule> | null | undefined;
    if (!modMaybe || typeof modMaybe._m68k_init !== 'function') {
      const keys = module && typeof module === 'object' ? Object.keys(module as Record<string, unknown>).slice(0, 20) : [];
      throw new Error(
        'Musashi Module shape unexpected: _m68k_init not found. ' +
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
