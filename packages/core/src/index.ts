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

  async stop(): Promise<Uint8Array> {
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
    this._musashi.init(this, config.rom, this.ram);
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

  async call(address: number): Promise<number> {
    return this._musashi.call(address);
  }

  async run(cycles: number): Promise<number> {
    return this._musashi.execute(cycles);
  }

  async step(): Promise<{ cycles: number; startPc: number; endPc: number; ppc?: number }> {
    const startPc = this._musashi.get_reg(M68kRegister.PC) >>> 0; // PC before executing
    // musashi.step() may return an unsigned long long (BigInt with WASM_BIGINT)
    // Normalize to Number before masking to avoid BigInt/Number mixing.
    const c = this._musashi.step();
    const cycles = Number(c) >>> 0;
    const endPcActual = this._musashi.get_reg(M68kRegister.PC) >>> 0; // PC after executing
    // Previous PC as reported by the core; may equal startPc
    const ppc = this._musashi.get_reg(M68kRegister.PPC) >>> 0;
    // Normalize endPc to decoded instruction size boundary when possible.
    // This avoids prefetch-related discrepancies in metadata while leaving core state intact.
    const size = this.getInstructionSize(startPc) >>> 0;
    const endPc = size > 0 ? ((startPc + size) >>> 0) : endPcActual;
    return { cycles, startPc, endPc, ppc };
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
    this._memReads.add(cb);
    this._updateMemTraceEnabled();
    return () => {
      this._memReads.delete(cb);
      this._updateMemTraceEnabled();
    };
  }

  onMemoryWrite(cb: MemoryAccessCallback): () => void {
    this._memWrites.add(cb);
    this._updateMemTraceEnabled();
    return () => {
      this._memWrites.delete(cb);
      this._updateMemTraceEnabled();
    };
  }

  // Called by MusashiWrapper when the core emits a memory event
  _handleMemoryRead(addr: number, size: 1 | 2 | 4, value: number, pc: number): void {
    if (this._memReads.size === 0) return;
    const ev: MemoryAccessEvent = { addr, size, value, pc };
    for (const cb of this._memReads) cb(ev);
  }

  _handleMemoryWrite(addr: number, size: 1 | 2 | 4, value: number, pc: number): void {
    if (this._memWrites.size === 0) return;
    const ev: MemoryAccessEvent = { addr, size, value, pc };
    for (const cb of this._memWrites) cb(ev);
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
