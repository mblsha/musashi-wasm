/**
 * Represents the state of the M68k CPU registers.
 * All values are 32-bit unsigned integers.
 */
export interface CpuRegisters {
  d0: number;
  d1: number;
  d2: number;
  d3: number;
  d4: number;
  d5: number;
  d6: number;
  d7: number;
  a0: number;
  a1: number;
  a2: number;
  a3: number;
  a4: number;
  a5: number;
  a6: number;
  sp: number; // Stack Pointer (A7)
  pc: number; // Program Counter
  sr: number; // Status Register
  ppc: number; // Previous Program Counter
}

// Enum of CPU register indices aligned with Musashi's m68k.h
// Values must stay in sync with the C enum m68k_register_t
export enum M68kReg {
  D0 = 0,
  D1 = 1,
  D2 = 2,
  D3 = 3,
  D4 = 4,
  D5 = 5,
  D6 = 6,
  D7 = 7,
  A0 = 8,
  A1 = 9,
  A2 = 10,
  A3 = 11,
  A4 = 12,
  A5 = 13,
  A6 = 14,
  A7 = 15,
  PC = 16,
  SR = 17,
  SP = 18,
  PPC = 19,
  USP = 20,
  ISP = 21,
  MSP = 22,
  SFC = 23,
  DFC = 24,
  VBR = 25,
  CACR = 26,
  CAAR = 27,
  PREF_ADDR = 32,
  PREF_DATA = 33,
  IR = 36,
  CPU_TYPE = 37,
}

/** A function to be executed when a specific address is hit during execution. */
export type HookCallback = (system: System) => void;

/** Memory access event payload for JS callbacks. */
export interface MemoryAccessEvent {
  addr: number;
  size: 1 | 2 | 4;
  value: number;
  pc: number;
}

/** Callback invoked on traced memory access. */
export type MemoryAccessCallback = (event: MemoryAccessEvent) => void;

/** Configuration for creating a new System instance. */
export interface SystemConfig {
  /** The ROM data for the system. */
  rom: Uint8Array;
  /** The size of the system's RAM in bytes. */
  ramSize: number;
}

/** Configuration for a Perfetto tracing session. */
export interface TraceConfig {
  /** Trace instruction execution slices. Defaults to false. */
  instructions?: boolean;
  /** Trace function calls, returns, and jumps. Defaults to false. */
  flow?: boolean;
  /** Trace memory write operations. Defaults to false. */
  memory?: boolean;
}

/** A map of memory addresses to human-readable names. */
export type SymbolMap = Record<number, string>;

/**
 * Interface for controlling and capturing Perfetto performance traces.
 * This functionality is only available if the core is built with Perfetto support.
 */
export interface Tracer {
  /**
   * Checks if the tracing functionality is available in the loaded Wasm module.
   * @returns `true` if tracing is supported, `false` otherwise.
   */
  isAvailable(): boolean;

  /**
   * Starts a new tracing session. Throws an error if a session is already active
   * or if tracing is not available.
   * @param config Configuration for which events to capture.
   */
  start(config?: TraceConfig): void;

  /**
   * Stops the current tracing session and returns the captured data.
   * Throws an error if no session is active.
   * @returns A promise that resolves to the trace data as a `Uint8Array`.
   */
  stop(): Promise<Uint8Array>;

  /**
   * Registers a map of addresses to function names. These names will appear
   * in the trace for better readability. Call this before starting a trace.
   */
  registerFunctionNames(symbols: SymbolMap): void;

  /**
   * Registers a map of addresses to memory region names.
   */
  registerMemoryNames(symbols: SymbolMap): void;
}

/**
 * The main interface for interacting with the M68k system. It provides
 * low-level control over memory, registers, and execution.
 */
export interface System {
  /** Reads an unsigned integer from the system's memory space. */
  read(address: number, size: 1 | 2 | 4): number;

  /** Writes an unsigned integer to the system's memory space. */
  write(address: number, size: 1 | 2 | 4, value: number): void;

  /** Reads a block of memory into a new byte array. */
  readBytes(address: number, length: number): Uint8Array;

  /** Writes a block of memory from a byte array. */
  writeBytes(address: number, data: Uint8Array): void;

  /** Returns a snapshot of the current CPU register values. */
  getRegisters(): CpuRegisters;

  /** Sets the value of a single CPU register. */
  setRegister<K extends keyof CpuRegisters>(register: K, value: number): void;

  /**
   * Executes a native subroutine at the given address. The promise resolves
   * when the subroutine returns (e.g., via an RTS instruction).
   * @returns The number of CPU cycles executed.
   */
  call(address: number): Promise<number>;

  /**
   * Runs the emulator for a specified number of CPU cycles.
   * @returns The number of CPU cycles actually executed.
   */
  run(cycles: number): Promise<number>;

  /**
   * Executes exactly one instruction and stops before the next one.
   * Returns execution metadata for the instruction.
   * - cycles: CPU cycles consumed by the instruction
   * - startPc: PC at instruction start
   * - endPc: PC after instruction completes
   * - ppc: optional previous PC reported by the core (usually equals startPc)
   */
  step(): Promise<{ cycles: number; startPc: number; endPc: number; ppc?: number }>;

  /** Resets the CPU to its initial state. */
  reset(): void;

  /**
   * Attaches a "probe" to an address. The callback is executed when the PC
   * hits this address, after which native execution continues.
   * @returns A function to remove the hook.
   */
  probe(address: number, callback: HookCallback): () => void;

  /**
   * Attaches an "override" to an address. The callback is executed instead
   * of the native code. The emulator executes an RTS immediately after.
   * @returns A function to remove the hook.
   */
  override(address: number, callback: HookCallback): () => void;

  /** Accesses the optional Perfetto tracing functionality. */
  readonly tracer: Tracer;

  /** Disassembles a single instruction at the given address and returns a formatted string (or null if unavailable). */
  disassemble(address: number): string | null;

  /**
   * Returns the size in bytes of the instruction at the given PC.
   * Returns 0 if the disassembler is unavailable or decoding fails.
   */
  getInstructionSize(pc: number): number;

  /**
   * Register a callback for memory reads performed by the CPU. The callback receives
   * the accessed address, size (1/2/4), value read, and the PC of the instruction.
   * Returns a function to unsubscribe.
   */
  onMemoryRead(cb: MemoryAccessCallback): () => void;

  /**
   * Register a callback for memory writes performed by the CPU. The callback receives
   * the accessed address, size (1/2/4), value written, and the PC of the instruction.
   * Returns a function to unsubscribe.
   */
  onMemoryWrite(cb: MemoryAccessCallback): () => void;
}
