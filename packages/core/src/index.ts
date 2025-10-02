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
  MemoryTraceSource,
  MemoryLayout,
  MemoryRegion,
  MirrorRegion,
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
  MemoryAccessCallback,
  MemoryAccessEvent,
  MemoryTraceSource,
  MemoryLayout,
  MemoryRegion,
  MirrorRegion,
};
export { M68kRegister } from '@m68k/common';
export type {
  ReadMemoryCallback,
  WriteMemoryCallback,
  PCHookCallback,
} from '@m68k/common';

// --- Private Implementation ---

const normalizeTraceAddress = (value: number): number => (value >>> 0) & 0x00ffffff;
const formatTraceHex = (value: number): string => `0x${(value >>> 0).toString(16)}`;

const parseBooleanEnv = (value: string | undefined): boolean => {
  if (!value) return false;
  const normalized = value.trim().toLowerCase();
  return normalized === '1' || normalized === 'true' || normalized === 'yes' || normalized === 'on';
};

const env = typeof process !== 'undefined' ? process.env : undefined;

const TRACE_ADDRESSES: ReadonlySet<number> = (() => {
  const targets = new Set<number>();
  if (!env) return targets;

  const addToken = (token: string) => {
    const value = token.trim();
    if (!value) return;
    const parsed = value.startsWith('0x') || value.startsWith('0X')
      ? Number.parseInt(value, 16)
      : Number.parseInt(value, 10);
    if (Number.isNaN(parsed)) return;
    targets.add(normalizeTraceAddress(parsed));
  };

  if (parseBooleanEnv(env.MUSASHI_TRACE_A0)) {
    addToken('0x00100a80');
  }

  if (env.MUSASHI_TRACE_ADDRS) {
    for (const token of env.MUSASHI_TRACE_ADDRS.split(',')) {
      addToken(token);
    }
  }

  return targets;
})();

const TRACE_INCLUDE_STACK = env ? parseBooleanEnv(env.MUSASHI_TRACE_STACK) : false;
const TRACE_LEVEL = (env?.MUSASHI_TRACE_LEVEL ?? 'warn').toLowerCase();
const TRACE_ENABLED = TRACE_ADDRESSES.size > 0;

const TRACE_LOGGER: (payload: unknown) => void = (() => {
  if (typeof console === 'undefined') {
    return () => {};
  }
  const consoleAny = console as unknown as Record<string, unknown>;
  let logFn: ((...args: unknown[]) => void) | undefined;
  const candidate = consoleAny[TRACE_LEVEL];
  if (typeof candidate === 'function') {
    logFn = candidate as (...args: unknown[]) => void;
  } else if (typeof console.warn === 'function') {
    logFn = (...args: unknown[]) => console.warn(...args);
  }
  if (!logFn) {
    return () => {};
  }
  return (payload: unknown) => {
    try {
      logFn('[musashi-trace]', payload);
    } catch {
      // ignore logging failures
    }
  };
})();

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
  private _memSequence = 0;
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

  readRaw8(address: number): number {
    return this._musashi.readRaw8(address);
  }

  writeRaw8(address: number, value: number): void {
    this._musashi.writeRaw8(address, value);
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

    // musashi.step() may return an unsigned long long (BigInt with WASM_BIGINT)
    // Normalize to Number before masking to avoid BigInt/Number mixing.
    const cyclesRaw = this._musashi.step();
    const totalCycles = Number(cyclesRaw) >>> 0;

    const afterPc = this._musashi.get_reg(M68kRegister.PC) >>> 0;
    const ppc = this._musashi.get_reg(M68kRegister.PPC) >>> 0;
    const spNow = this._musashi.get_reg(M68kRegister.A7) >>> 0;
    const spDelta = initialSp >= spNow ? (initialSp - spNow) >>> 0 : 0;

    const grewStack = spNow < initialSp && spDelta >= 6;
    const instSize = this.getInstructionSize(startPc) >>> 0;
    const sequentialPc = instSize ? ((startPc + instSize) >>> 0) : startPc;
    const exceptionDetected = grewStack && afterPc !== sequentialPc;

    let finalPc = afterPc >>> 0;
    let finalPpc = ppc >>> 0;

    if (exceptionDetected) {
      finalPc = afterPc >>> 0;
      finalPpc = startPc >>> 0;
      this._musashi.set_reg(M68kRegister.PC, finalPc);
      this._musashi.set_reg(M68kRegister.PPC, finalPpc);
    }

    return { cycles: totalCycles >>> 0, startPc, endPc: finalPc >>> 0, ppc: finalPpc };
  }

  reset(): void {
    this._musashi.pulse_reset();
  }

  probe(address: number, callback: HookCallback): () => void {
    return this._registerHook(this._hooks.probes, address, callback);
  }

  override(address: number, callback: HookCallback): () => void {
    return this._registerHook(this._hooks.overrides, address, callback);
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
  _handleMemoryRead(
    addr: number,
    size: 1 | 2 | 4,
    value: number,
    pc: number,
    ppc?: number,
    source?: MemoryTraceSource
  ): void {
    this._dispatchMemoryEvent(this._memReads, { addr, size, value, pc, kind: 'read', ppc, source });
  }

  _handleMemoryWrite(
    addr: number,
    size: 1 | 2 | 4,
    value: number,
    pc: number,
    ppc?: number,
    source?: MemoryTraceSource
  ): void {
    this._dispatchMemoryEvent(this._memWrites, { addr, size, value, pc, kind: 'write', ppc, source });
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
    const payload = event as MemoryAccessEvent & { sequence?: number };
    payload.sequence = ++this._memSequence;

    if (TRACE_ENABLED) {
      this._maybeTraceMemoryEvent(payload);
    }

    if (listeners.size === 0) {
      return;
    }
    for (const cb of listeners) {
      cb(payload);
    }
  }

  private _maybeTraceMemoryEvent(event: MemoryAccessEvent): void {
    if (!TRACE_ENABLED) return;
    const normalizedAddr = normalizeTraceAddress(event.addr);
    if (!TRACE_ADDRESSES.has(normalizedAddr)) return;

    const ppc = this._musashi.get_reg(M68kRegister.PPC) >>> 0;
    const sp = this._musashi.get_reg(M68kRegister.A7) >>> 0;
    const sr = this._musashi.get_reg(M68kRegister.SR) >>> 0;

    const enriched = event as MemoryAccessEvent & {
      ppc?: number;
      sp?: number;
      sr?: number;
      timestamp?: number;
      stack?: string;
    };

    enriched.ppc = ppc;
    enriched.sp = sp;
    enriched.sr = sr;
    enriched.timestamp = Date.now();

    let stack: string | undefined;
    if (TRACE_INCLUDE_STACK) {
      stack = new Error('musashi-trace').stack;
      if (stack) {
        enriched.stack = stack;
      }
    }

    const payload: Record<string, unknown> = {
      seq: event.sequence,
      kind: event.kind ?? 'write',
      addr: formatTraceHex(normalizedAddr),
      size: event.size,
      value: formatTraceHex(event.value),
      pc: formatTraceHex(normalizeTraceAddress(event.pc)),
      ppc: formatTraceHex(normalizeTraceAddress(ppc)),
      sp: formatTraceHex(normalizeTraceAddress(sp)),
      sr: formatTraceHex(sr),
    };

    if (TRACE_INCLUDE_STACK && stack) {
      payload.stack = stack;
    }

    TRACE_LOGGER(payload);
  }

  private _updateMemTraceEnabled(): void {
    const want = this._memReads.size > 0 || this._memWrites.size > 0;
    this._musashi.setMemoryTraceEnabled(want);
  }

  private _registerHook(
    collection: Map<number, HookCallback>,
    address: number,
    callback: HookCallback
  ): () => void {
    collection.set(address, callback);
    this._musashi.add_pc_hook_addr(address);
    return () => {
      if (collection.get(address) === callback) {
        collection.delete(address);
      }
    };
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
