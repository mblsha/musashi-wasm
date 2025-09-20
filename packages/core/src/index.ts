import type {
  System,
  SystemConfig,
  CpuRegisters,
  HookCallback,
  Tracer,
  TraceConfig,
  SymbolMap,
  MemoryAccessCallback,
  MemoryAccessEvent,
} from './types.js';
import { M68kRegister } from '@m68k/common';
import { MusashiWrapper, getModule } from './musashi-wrapper.js';

// Re-export types
export type {
  System,
  SystemConfig,
  CpuRegisters,
  HookCallback,
  Tracer,
  TraceConfig,
  SymbolMap,
};
export { M68kRegister } from '@m68k/common';

// --- Private Implementation ---

// A map from register names to their numeric index in Musashi.
const REGISTER_MAP: { [K in keyof CpuRegisters]: M68kRegister } = {
  d0: M68kRegister.D0,
  d1: M68kRegister.D1,
  d2: M68kRegister.D2,
  d3: M68kRegister.D3,
  d4: M68kRegister.D4,
  d5: M68kRegister.D5,
  d6: M68kRegister.D6,
  d7: M68kRegister.D7,
  a0: M68kRegister.A0,
  a1: M68kRegister.A1,
  a2: M68kRegister.A2,
  a3: M68kRegister.A3,
  a4: M68kRegister.A4,
  a5: M68kRegister.A5,
  a6: M68kRegister.A6,
  sp: M68kRegister.A7, // a7
  pc: M68kRegister.PC,
  sr: M68kRegister.SR,
  ppc: M68kRegister.PPC,
};

class TracerImpl implements Tracer {
  private _musashi: MusashiWrapper;
  private _active = false;

  constructor(musashi: MusashiWrapper) {
    this._musashi = musashi;
  }

  isAvailable(): boolean {
    return this._musashi.isPerfettoAvailable();
  }

  start(config: TraceConfig = {}): void {
    if (!this.isAvailable()) {
      throw new Error(
        'Perfetto tracing is not available in this build of the m68k core.'
      );
    }
    if (this._active) {
      throw new Error('A tracing session is already active.');
    }

    if (this._musashi.perfettoInit('m68k-ts') !== 0) {
      throw new Error('Failed to initialize Perfetto tracing session.');
    }

    // Enable the underlying m68ktrace framework which Perfetto uses
    this._musashi.traceEnable(true);

    // Configure trace features
    this._musashi.perfettoEnableFlow(!!config.flow);
    this._musashi.perfettoEnableMemory(!!config.memory);
    this._musashi.perfettoEnableInstructions(!!config.instructions);

    this._active = true;
  }

  stop(): Uint8Array {
    if (!this._active) {
      throw new Error('No active tracing session to stop.');
    }

    // Ensure any open function call slices are closed before exporting
    this._musashi.perfettoCleanupSlices();

    const traceData = this._musashi.perfettoExportTrace();

    // Clean up the session
    this._musashi.perfettoDestroy();
    this._musashi.traceEnable(false);
    this._active = false;

    if (traceData === null) {
      throw new Error('Failed to export Perfetto trace data.');
    }

    return traceData;
  }

  private _registerSymbols(
    symbols: SymbolMap,
    register: (addr: number, name: string) => void
  ): void {
    if (!this.isAvailable()) return;
    for (const [address, name] of Object.entries(symbols)) {
      const addrNum = Number(address);
      if (!Number.isNaN(addrNum)) {
        register(addrNum, name);
      }
    }
  }

  registerFunctionNames(symbols: SymbolMap): void {
    this._registerSymbols(symbols, (addr, name) =>
      this._musashi.registerFunctionName(addr, name)
    );
  }

  registerMemoryNames(symbols: SymbolMap): void {
    this._registerSymbols(symbols, (addr, name) =>
      this._musashi.registerMemoryName(addr, name)
    );
  }
}

class SystemImpl implements System {
  private _musashi: MusashiWrapper;
  readonly ram: Uint8Array;
  private _rom: Uint8Array;
  private _hooks = {
    probes: new Map<number, HookCallback>(),
    overrides: new Map<number, HookCallback>(),
  };
  private _memReads = new Set<MemoryAccessCallback>();
  private _memWrites = new Set<MemoryAccessCallback>();
  readonly tracer: Tracer;

  constructor(musashi: MusashiWrapper, config: SystemConfig) {
    this._musashi = musashi;
    this._rom = config.rom;
    this.ram = new Uint8Array(config.ramSize);
    this.tracer = new TracerImpl(musashi);

    // Initialize Musashi with memory regions and hooks
    this._musashi.init(this, config.rom, this.ram, config.memoryLayout);
  }

  // (no fusion-specific instrumentation helpers)

  // Single-string disassembly (no address prefix)
  disassemble(address: number): string | null {
    const pc = address >>> 0;
    const one = this._musashi.disassemble(pc);
    if (!one) return null;
    return one.text;
    }
  getInstructionSize(pc: number): number {
    const one = this._musashi.disassemble(pc >>> 0);
    return one ? (one.size >>> 0) : 0;
  }
  read(address: number, size: 1 | 2 | 4): number {
    return this._musashi.read_memory(address, size);
  }

  write(address: number, size: 1 | 2 | 4, value: number): void {
    this._musashi.write_memory(address, size, value);
  }

  readBytes(address: number, length: number): Uint8Array {
    const buffer = new Uint8Array(length);
    for (let i = 0; i < length; i++) {
      buffer[i] = this.read(address + i, 1);
    }
    return buffer;
  }

  writeBytes(address: number, data: Uint8Array): void {
    for (let i = 0; i < data.length; i++) {
      this.write(address + i, 1, data[i]);
    }
  }

  getRegisters(): CpuRegisters {
    const regs: Partial<CpuRegisters> = {};
    for (const key in REGISTER_MAP) {
      const regKey = key as keyof CpuRegisters;
      regs[regKey] = this._musashi.get_reg(REGISTER_MAP[regKey]);
    }
    return regs as CpuRegisters;
  }

  setRegister<K extends keyof CpuRegisters>(register: K, value: number): void {
    const index = REGISTER_MAP[register];
    if (index !== undefined) {
      this._musashi.set_reg(index, value);
    }
  }

  call(address: number): number {
    return this._musashi.call(address);
  }

  run(cycles: number): number {
    return this._musashi.execute(cycles);
  }

  step(): { cycles: number; startPc: number; endPc: number; ppc?: number } {
    const startPc = this._musashi.get_reg(M68kRegister.PC) >>> 0; // PC before executing
    const initialSp = this._musashi.get_reg(M68kRegister.A7) >>> 0;

    let totalCycles = 0;
    let currentStartPc = startPc;
    let previousSp = initialSp;
    let finalPc = startPc;
    let ppc = this._musashi.get_reg(M68kRegister.PPC) >>> 0;

    for (let iteration = 0; iteration < 2; iteration++) {
      // musashi.step() may return an unsigned long long (BigInt with WASM_BIGINT)
      // Normalize to Number before masking to avoid BigInt/Number mixing.
      const cyclesRaw = this._musashi.step();
      totalCycles += Number(cyclesRaw) >>> 0;

      const afterPc = this._musashi.get_reg(M68kRegister.PC) >>> 0;
      finalPc = afterPc;
      ppc = this._musashi.get_reg(M68kRegister.PPC) >>> 0;

      const spNow = this._musashi.get_reg(M68kRegister.A7) >>> 0;
      const spDelta = previousSp >= spNow ? (previousSp - spNow) >>> 0 : 0;

      const exceptionDetected =
        iteration === 0 &&
        spNow < previousSp &&
        spDelta >= 6 &&
        afterPc < startPc;

      if (exceptionDetected) {
        // Execute the handler from its true start. If the core already
        // normalized PC to the handler entry, use it directly; otherwise fall
        // back to compensating for the prefetch word.
        let handlerPc = afterPc >>> 0;
        if (this.getInstructionSize(handlerPc) === 0 && handlerPc >= 2) {
          handlerPc = (handlerPc - 2) >>> 0;
        }
        this._musashi.set_reg(M68kRegister.PC, handlerPc);
        currentStartPc = handlerPc;
        previousSp = spNow;
        // Execute exactly one instruction from the handler on the next loop iteration
        continue;
      }

      break;
    }

    // Normalize core state so subsequent reads see the final instruction boundary.
    this._musashi.set_reg(M68kRegister.PPC, currentStartPc >>> 0);
    this._musashi.set_reg(M68kRegister.PC, finalPc >>> 0);

    return { cycles: totalCycles >>> 0, startPc, endPc: finalPc >>> 0, ppc };
  }

  reset(): void {
    this._musashi.pulse_reset();
  }

  probe(address: number, callback: HookCallback): () => void {
    this._hooks.probes.set(address, callback);
    this._musashi.add_pc_hook_addr(address);
    return () => this._hooks.probes.delete(address);
  }

  override(address: number, callback: HookCallback): () => void {
    this._hooks.overrides.set(address, callback);
    this._musashi.add_pc_hook_addr(address);
    return () => this._hooks.overrides.delete(address);
  }

  // --- Internal methods for the Musashi wrapper ---
  _handlePCHook(pc: number): boolean {
    const probe = this._hooks.probes.get(pc);
    if (probe) {
      probe(this);
      return false; // Continue execution
    }

    const override = this._hooks.overrides.get(pc);
    if (override) {
      override(this);
      return true; // Stop and execute RTS
    }

    return false; // Continue execution
  }

  cleanup(): void {
    this._musashi.cleanup();
  }

  // --- Memory Trace (read/write) ---
  onMemoryRead(cb: MemoryAccessCallback): () => void {
    return this._addMemoryListener(this._memReads, cb);
  }

  onMemoryWrite(cb: MemoryAccessCallback): () => void {
    return this._addMemoryListener(this._memWrites, cb);
  }

  // Called by MusashiWrapper when the core emits a memory event
  _handleMemoryRead(addr: number, size: 1 | 2 | 4, value: number, pc: number): void {
    this._dispatchMemoryEvent(this._memReads, { addr, size, value, pc });
  }

  _handleMemoryWrite(addr: number, size: 1 | 2 | 4, value: number, pc: number): void {
    this._dispatchMemoryEvent(this._memWrites, { addr, size, value, pc });
  }

  private _addMemoryListener(
    collection: Set<MemoryAccessCallback>,
    callback: MemoryAccessCallback
  ): () => void {
    collection.add(callback);
    this._updateMemTraceEnabled();
    return () => {
      collection.delete(callback);
      this._updateMemTraceEnabled();
    };
  }

  private _dispatchMemoryEvent(
    listeners: Set<MemoryAccessCallback>,
    event: MemoryAccessEvent
  ): void {
    if (listeners.size === 0) {
      return;
    }
    for (const cb of listeners) {
      cb(event);
    }
  }

  private _updateMemTraceEnabled(): void {
    const want = this._memReads.size > 0 || this._memWrites.size > 0;
    this._musashi.setMemoryTraceEnabled(want);
  }
}

// --- Public API ---

/**
 * Asynchronously creates and initializes a new M68k system instance.
 * @param config The system configuration.
 */
export async function createSystem(config: SystemConfig): Promise<System> {
  const musashiWrapper = await getModule();
  return new SystemImpl(musashiWrapper, config);
}
