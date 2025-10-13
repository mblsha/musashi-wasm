// This is a wrapper around the Emscripten module

// These types are placeholders for Emscripten's internal types
type EmscriptenBuffer = number;
type EmscriptenFunction = number;
import { M68kRegister } from '@m68k/common';
import type { MemoryLayout, MemoryTraceSource } from './types.js';
import { mask24 } from './address-utils.js';

type RuntimeTag = 'node' | 'browser';

const detectRuntime = (): RuntimeTag => {
  const scope = globalThis as typeof globalThis & {
    process?: NodeJS.Process;
    navigator?: { userAgent?: string };
    importScripts?: unknown;
    document?: unknown;
    window?: unknown;
  };

  const isNodeProcess =
    typeof scope.process !== 'undefined' &&
    scope.process?.release?.name === 'node';

  const isBrowserLike =
    typeof scope.importScripts === 'function' ||
    typeof scope.navigator?.userAgent === 'string' ||
    typeof scope.document === 'object' ||
    typeof scope.window === 'object';

  return isNodeProcess && !isBrowserLike ? 'node' : 'browser';
};

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
  _set_read8_callback?(f: EmscriptenFunction): void;
  _set_write8_callback?(f: EmscriptenFunction): void;
  _set_probe_callback?(f: EmscriptenFunction): void;
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
  _handleMemoryRead?(addr: number, size: 1 | 2 | 4, value: number, pc: number, ppc?: number, source?: MemoryTraceSource): void;
  _handleMemoryWrite?(addr: number, size: 1 | 2 | 4, value: number, pc: number, ppc?: number, source?: MemoryTraceSource): void;
}

export class MusashiWrapper {
  private _module: MusashiEmscriptenModule;
  private _system!: SystemBridge; // Reference to SystemImpl
  private _memory: Uint8Array = new Uint8Array(0); // allocated in init()
  private _ramWindows: Array<{ start: number; length: number; offset: number }> = [];
  private _readFunc: EmscriptenFunction = 0;
  private _writeFunc: EmscriptenFunction = 0;
  private _probeFunc: EmscriptenFunction = 0;
  private _memTraceFunc: EmscriptenFunction = 0;
  private _memTraceActive = false;
  private readonly _traceAvailable: boolean;
  // No JS sentinel state; C++ session owns sentinel behavior.
  // CPU type for disassembler (68000)
  private readonly CPU_68000 = 0;

  constructor(module: MusashiEmscriptenModule) {
    this._module = module;
    this._traceAvailable = Boolean(
      module._m68k_set_trace_mem_callback &&
      module._m68k_trace_enable &&
      module._m68k_trace_set_mem_enabled
    );
  }

  private applyDefaultMemoryMapping(rom: Uint8Array, ram: Uint8Array): void {
    this._memory.set(rom, 0x000000);
    this._memory.set(ram, 0x100000);
    this._ramWindows.push({ start: 0x100000, length: ram.length >>> 0, offset: 0 });
  }

  private getActiveMemoryLayout(memoryLayout?: MemoryLayout): MemoryLayout | undefined {
    if (!memoryLayout) {
      return undefined;
    }

    const hasRegions = (memoryLayout.regions?.length ?? 0) > 0;
    const hasMirrors = (memoryLayout.mirrors?.length ?? 0) > 0;

    return hasRegions || hasMirrors ? memoryLayout : undefined;
  }

  init(system: SystemBridge, rom: Uint8Array, ram: Uint8Array, memoryLayout?: MemoryLayout) {
    this._system = system;

    const layout = this.getActiveMemoryLayout(memoryLayout);
    const requestedMinCapacity = (memoryLayout?.minimumCapacity ?? 0) >>> 0;

    // --- Allocate unified memory based on layout or defaults ---
    const DEFAULT_CAPACITY = 2 * 1024 * 1024; // 2MB
    let capacity = DEFAULT_CAPACITY >>> 0;
    this._ramWindows = [];

    if (layout) {
      let maxEnd = 0;
      for (const r of layout.regions ?? []) {
        const start = r.start >>> 0;
        const length = r.length >>> 0;
        if (length === 0) continue;
        const end = (start + length) >>> 0;
        if (end > maxEnd) maxEnd = end;
      }
      for (const m of layout.mirrors ?? []) {
        const destStart = m.start >>> 0;
        const destEnd = (destStart + (m.length >>> 0)) >>> 0;
        if (destEnd > maxEnd) maxEnd = destEnd;
        const srcStart = m.mirrorFrom >>> 0;
        const srcEnd = (srcStart + (m.length >>> 0)) >>> 0;
        if (srcEnd > maxEnd) maxEnd = srcEnd;
      }
      capacity = Math.max(maxEnd, requestedMinCapacity) >>> 0;
    }

    capacity = Math.max(capacity, requestedMinCapacity) >>> 0;

    this._memory = new Uint8Array(capacity);

    // --- Initialize regions ---
    if (layout) {
      // Base regions
      for (const r of layout.regions ?? []) {
        const start = r.start >>> 0;
        const length = r.length >>> 0;
        const srcOff = (r.sourceOffset ?? 0) >>> 0;
        if (length === 0) continue;
        // Bounds validation
        if (start + length > this._memory.length) {
          throw new Error(`Region out of bounds: start=0x${start.toString(16)}, len=0x${length.toString(16)}, cap=0x${this._memory.length.toString(16)}`);
        }
        if (r.source === 'rom') {
          if (srcOff + length > rom.length) {
            throw new Error(`ROM source out of range: off=0x${srcOff.toString(16)}, len=0x${length.toString(16)}, rom=0x${rom.length.toString(16)}`);
          }
          this._memory.set(rom.subarray(srcOff, srcOff + length), start);
        } else if (r.source === 'ram') {
          if (srcOff + length > ram.length) {
            throw new Error(`RAM source out of range: off=0x${srcOff.toString(16)}, len=0x${length.toString(16)}, ram=0x${ram.length.toString(16)}`);
          }
          this._memory.set(ram.subarray(srcOff, srcOff + length), start);
          // Record window for write-back to RAM buffer
          this._ramWindows.push({ start, length, offset: srcOff });
        } else if (r.source === 'zero') {
          this._memory.fill(0, start, start + length);
        }
      }
      // Mirrors
      for (const m of layout.mirrors ?? []) {
        const start = m.start >>> 0;
        const length = m.length >>> 0;
        const from = m.mirrorFrom >>> 0;
        if (length === 0) continue;
        if (from + length > this._memory.length || start + length > this._memory.length) {
          throw new Error(`Mirror out of bounds: from=0x${from.toString(16)}, start=0x${start.toString(16)}, len=0x${length.toString(16)}, cap=0x${this._memory.length.toString(16)}`);
        }
        const src = this._memory.subarray(from, from + length);
        this._memory.set(src, start);
      }
    } else {
      // Backward-compatible default mapping
      this.applyDefaultMemoryMapping(rom, ram);
    }

    // Setup callbacks (size-aware read/write; PC hook)
    const readSizedPtr = this._module.addFunction((addr: number, size: number) =>
      this.readHandler(addr >>> 0, (size | 0) as 1 | 2 | 4), 'iii');
    const writeSizedPtr = this._module.addFunction((addr: number, size: number, val: number) =>
      this.writeHandler(addr >>> 0, (size | 0) as 1 | 2 | 4, val >>> 0), 'viii');
    this._probeFunc = this._module.addFunction((addr: number) =>
      (this._system._handlePCHook(addr >>> 0) ? 1 : 0), 'ii');

    this._readFunc = readSizedPtr;
    this._writeFunc = writeSizedPtr;

    // Register callbacks with C
    this._module._set_read_mem_func(readSizedPtr);
    this._module._set_write_mem_func(writeSizedPtr);
    this._module._set_pc_hook_func(this._probeFunc);

    // Respect any reset vector supplied in the ROM. Only fall back to the
    // legacy defaults when individual fields are zero to preserve historic
    // behaviour for images without a vector table.
    const defaultSp = 0x0010_8000 >>> 0;
    const defaultPc = 0x0000_0400 >>> 0;
    const existingSp = this.read32BE(0x0000);
    const existingPc = this.read32BE(0x0004);

    const vectorSp = existingSp !== 0 ? existingSp >>> 0 : defaultSp;
    const vectorPc = existingPc !== 0 ? existingPc >>> 0 : defaultPc;

    if (existingSp === 0) {
      this.write32BE(0x0000, vectorSp);
    }
    if (existingPc === 0) {
      this.write32BE(0x0004, vectorPc);
    }

    if ((vectorPc & 1) !== 0) {
      throw new Error(
        `Reset vector PC must be even (saw 0x${vectorPc.toString(16)})`
      );
    }

    // Initialize CPU
    this._module._m68k_init();
    this._module._m68k_set_context?.(0);

    // Reset CPU (will read vectors we just wrote)
    this._module._m68k_pulse_reset();

    // Verify initialization
    const pc = this._module._m68k_get_reg(0, M68kRegister.PC) >>> 0;
    const normalizedExpectedPc = (vectorPc & 0x00ff_ffff) & ~1;
    if (pc !== normalizedExpectedPc) {
      throw new Error(
        `CPU not properly initialized, PC=0x${pc.toString(16)} (expected 0x${normalizedExpectedPc.toString(16)})`
      );
    }
  }

  private write32BE(addr: number, value: number): void {
    this._memory[addr + 0] = (value >>> 24) & 0xff;
    this._memory[addr + 1] = (value >>> 16) & 0xff;
    this._memory[addr + 2] = (value >>> 8) & 0xff;
    this._memory[addr + 3] = value & 0xff;
  }

  private read32BE(addr: number): number {
    return (
      (this._memory[addr + 0] << 24) |
      (this._memory[addr + 1] << 16) |
      (this._memory[addr + 2] << 8) |
      this._memory[addr + 3]
    ) >>> 0;
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
    // Defer timeslice to C++ default by passing 0
    const ret = callUntil(address >>> 0, 0 as unknown as number);
    return typeof ret === 'bigint' ? Number(ret) >>> 0 : (ret as number) >>> 0;
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

  private findRamWindowForAddress(address: number) {
    const addr = address >>> 0;
    for (const window of this._ramWindows) {
      const start = window.start >>> 0;
      const end = start + (window.length >>> 0);
      if (addr >= start && addr < end) {
        return window;
      }
    }
    return undefined;
  }

  private getTraceRegisters(
    options?: { fallbackPc?: number }
  ): { pc: number; ppc: number } {
    const pc = mask24(this._module._m68k_get_reg(0, M68kRegister.PC) >>> 0);
    let ppc = pc;
    try {
      ppc = mask24(this._module._m68k_get_reg(0, M68kRegister.PPC) >>> 0);
    } catch {
      if (options?.fallbackPc !== undefined) {
        ppc = mask24(options.fallbackPc >>> 0);
      }
    }
    return { pc, ppc };
  }

  private isAccessWithinMemory(address: number, size: 1 | 2 | 4): boolean {
    const addr = address >>> 0;
    if (addr >= this._memory.length) return false;
    const end = addr + size;
    if (end > this._memory.length) return false;
    const window = this.findRamWindowForAddress(addr);
    if (window && end > window.start + window.length) {
      return false;
    }
    return true;
  }

  private writeByteToMemory(
    address: number,
    byte: number,
    hasWindowsOverride?: boolean,
    ramBuffer?: Uint8Array
  ): void {
    const addr = address >>> 0;
    const value = byte & 0xff;
    this._memory[addr] = value;

    const hasWindows = hasWindowsOverride ?? this._ramWindows.length > 0;
    if (!hasWindows) {
      return;
    }

    const window = this.findRamWindowForAddress(addr);
    if (!window) {
      return;
    }

    const ram = ramBuffer ?? this._system.ram;
    const ramIndex = (window.offset + (addr - window.start)) >>> 0;
    if (ramIndex < ram.length) {
      ram[ramIndex] = value;
    }
  }

  read_memory(address: number, size: 1 | 2 | 4): number {
    const addr = address >>> 0;
    if (!this.isAccessWithinMemory(addr, size)) return 0;
    let result: number;
    if (size === 1) {
      result = this._memory[addr];
    } else if (size === 2) {
      result = (this._memory[addr] << 8) | this._memory[addr + 1];
    } else {
      result = (
        (this._memory[addr] << 24) |
        (this._memory[addr + 1] << 16) |
        (this._memory[addr + 2] << 8) |
        this._memory[addr + 3]
      ) >>> 0;
    }

    if (!this._traceAvailable) {
      const { pc, ppc } = this.getTraceRegisters();
      this._system._handleMemoryRead?.(addr >>> 0, size, result >>> 0, pc, ppc, 'wrapper-fallback');
    }

    return result >>> 0;
  }

  write_memory(address: number, size: 1 | 2 | 4, value: number) {
    const addr = address >>> 0;
    // Respect bounds strictly: ignore cross-boundary writes
    if (!this.isAccessWithinMemory(addr, size)) return;
    const maskedValue = value >>> 0;
    const hasWindows = this._ramWindows.length > 0;
    const ram = this._system.ram;

    for (let i = 0; i < size; i++) {
      const shift = (size - 1 - i) * 8;
      const byte = (maskedValue >> shift) & 0xff;
      const a = (addr + i) >>> 0;
      this.writeByteToMemory(a, byte, hasWindows, ram);
    }

    if (!this._traceAvailable) {
      const { pc, ppc } = this.getTraceRegisters();
      this._system._handleMemoryWrite?.(addr >>> 0, size, maskedValue, pc, ppc, 'wrapper-fallback');
    }
  }

  readRaw8(address: number): number {
    const addr = address >>> 0;
    if (!this.isAccessWithinMemory(addr, 1)) {
      return 0;
    }
    return this._memory[addr] & 0xff;
  }

  writeRaw8(address: number, value: number): void {
    const addr = address >>> 0;
    if (!this.isAccessWithinMemory(addr, 1)) {
      return;
    }
    const byte = value & 0xff;
    this.writeByteToMemory(addr, byte);
  }

  // --- Memory Trace Hook Bridge ---
  // Enable/disable forwarding of core memory trace events to SystemImpl
  setMemoryTraceEnabled(enable: boolean): void {
    if (!this._traceAvailable) {
      this._memTraceActive = enable;
      return;
    }
    if (enable && !this._memTraceActive) {
      if (!this._memTraceFunc) {
        const makeCb = () => (
          type: number,
          pcParam: number,
          addr: number,
          value: number,
          size: number,
          _cycles: unknown
        ) => {
          const s = (size | 0) as 1 | 2 | 4;
          const a = addr >>> 0;
          const v = value >>> 0;
          const p = mask24(pcParam >>> 0);
          const { ppc } = this.getTraceRegisters({ fallbackPc: p });

          if (type === 0) {
            this._system._handleMemoryRead?.(a, s, v, p, ppc, 'core-trace');
            return 0;
          }

          this._system._handleMemoryWrite?.(a, s, v, p, ppc, 'core-trace');
          return 0;
        };
        try {
          this._memTraceFunc = this._module.addFunction(makeCb(), 'iiiiiij');
        } catch {
          this._memTraceFunc = this._module.addFunction(makeCb(), 'iiiiiii');
        }
      }
      this._module._m68k_set_trace_mem_callback!(this._memTraceFunc);
      this._module._m68k_trace_enable!(1);
      this._module._m68k_trace_set_mem_enabled!(1);
      this._memTraceActive = true;
    } else if (!enable && this._memTraceActive) {
      this._module._m68k_set_trace_mem_callback!(0 as unknown as number);
      this._module._m68k_trace_set_mem_enabled!(0);
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

  private withHeapString<T>(value: string, fn: (ptr: number) => T): T {
    const malloc = this._module._malloc;
    const free = this._module._free;
    const heap = this._module.HEAPU8;
    if (
      typeof malloc !== 'function' ||
      typeof free !== 'function' ||
      !(heap instanceof Uint8Array)
    ) {
      throw new Error('Musashi module does not expose heap string helpers.');
    }
    const ptr = malloc(value.length + 1);
    for (let i = 0; i < value.length; i++) {
      heap[ptr + i] = value.charCodeAt(i) & 0xff;
    }
    heap[ptr + value.length] = 0;
    try {
      return fn(ptr);
    } finally {
      free(ptr);
    }
  }

  perfettoInit(processName: string): number {
    const init = this._module._m68k_perfetto_init;
    if (!init) return -1;
    return this.withHeapString(processName, (namePtr) => init(namePtr));
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
    this.withHeapString(name, (namePtr) => {
      func.call(this._module, address, namePtr);
    });
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
  const runtime = detectRuntime();
  const isNode = runtime === 'node';

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
