import type {
  System,
  SystemConfig,
  CpuRegisters,
  HookCallback,
  Tracer,
  TraceConfig,
  SymbolMap,
} from './types.js';
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

// --- Private Implementation ---

// A map from register names to their numeric index in Musashi.
const REGISTER_MAP: { [K in keyof CpuRegisters]: number } = {
  d0: 0,
  d1: 1,
  d2: 2,
  d3: 3,
  d4: 4,
  d5: 5,
  d6: 6,
  d7: 7,
  a0: 8,
  a1: 9,
  a2: 10,
  a3: 11,
  a4: 12,
  a5: 13,
  a6: 14,
  sp: 15, // a7
  pc: 16,
  sr: 17,
  ppc: 19,
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

    return Promise.resolve(traceData);
  }

  registerFunctionNames(symbols: SymbolMap): void {
    if (!this.isAvailable()) return;
    for (const address in symbols) {
      // Ensure address is a number, as keys can be strings
      const addrNum = Number(address);
      if (!isNaN(addrNum)) {
        this._musashi.registerFunctionName(addrNum, symbols[address]);
      }
    }
  }

  registerMemoryNames(symbols: SymbolMap): void {
    if (!this.isAvailable()) return;
    for (const address in symbols) {
      const addrNum = Number(address);
      if (!isNaN(addrNum)) {
        this._musashi.registerMemoryName(addrNum, symbols[address]);
      }
    }
  }
}

class SystemImpl implements System {
  private _musashi: MusashiWrapper;
  private _ram: Uint8Array;
  private _rom: Uint8Array;
  private _hooks = {
    probes: new Map<number, HookCallback>(),
    overrides: new Map<number, HookCallback>(),
  };
  readonly tracer: Tracer;

  constructor(musashi: MusashiWrapper, config: SystemConfig) {
    this._musashi = musashi;
    this._rom = config.rom;
    this._ram = new Uint8Array(config.ramSize);
    this.tracer = new TracerImpl(musashi);

    // Initialize Musashi with memory regions and hooks
    this._musashi.init(this, config.rom, this._ram);
  }

  // --- Instrumentation helpers for advanced users ---
  enableSingleStep(enabled: boolean): void {
    this._musashi.setSingleStepMode(enabled);
  }

  onInstruction(cb: ((pc: number) => void) | undefined): void {
    this._musashi.onInstruction = cb;
  }

  onRead8(cb: ((addr: number, value: number) => void) | undefined): void {
    this._musashi.onRead8 = cb;
  }

  onWrite8(cb: ((addr: number, value: number) => void) | undefined): void {
    this._musashi.onWrite8 = cb;
  }

  // External memory interceptors (optional)
  setExternalRead8(fn?: (addr: number) => number | undefined): void {
    this._musashi.setExternalRead8(fn);
  }
  setExternalWrite8(fn?: (addr: number, value: number) => boolean | void): void {
    this._musashi.setExternalWrite8(fn);
  }

  // Configurable memory mapper (optional)
  setMemoryMapper(mapper?: (addr: number, isWrite: boolean) => { phys: number; allowWrite: boolean }): void {
    (this._musashi as any).setMemoryMapper?.(mapper);
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
      if (regKey === 'ppc') {
        // Prefer stable PPC captured at the end-of-instruction boundary so it
        // reflects the start PC of the just-retired instruction.
        const shadow = (this._musashi as any).getPpcShadow?.();
        regs.ppc = (shadow !== undefined ? shadow : this._musashi.get_reg(REGISTER_MAP.ppc)) >>> 0;
        continue;
      }
      regs[regKey] = this._musashi.get_reg(REGISTER_MAP[regKey]);
    }
    return regs as CpuRegisters;
  }

  disassemble(address: number): { text: string; size: number } | null {
    return (this._musashi as any).disassembleOne?.(address >>> 0) ?? null;
  }

  disassembleSequence(address: number, count: number): Array<{ pc: number; text: string; size: number }> {
    const out: Array<{ pc: number; text: string; size: number }> = [];
    let pc = address >>> 0;
    for (let i = 0; i < count; i++) {
      const one = this.disassemble(pc);
      if (!one) break;
      out.push({ pc, text: one.text, size: one.size >>> 0 });
      pc = (pc + (one.size >>> 0)) >>> 0;
    }
    return out;
  }

  setRegister<K extends keyof CpuRegisters>(register: K, value: number): void {
    const index = REGISTER_MAP[register];
    if (index !== undefined) {
      this._musashi.set_reg(index, value);
    }
  }

  async call(address: number): Promise<number> {
    return Promise.resolve(this._musashi.call(address));
  }

  async run(cycles: number): Promise<number> {
    return Promise.resolve(this._musashi.execute(cycles));
  }

  // Synchronous variant for embedding in synchronous hooks.
  runSync(cycles: number): number {
    return this._musashi.execute(cycles);
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
