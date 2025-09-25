import type {
  M68kRegister as CoreM68kRegister,
  ReadMemoryCallback as CoreReadMemoryCallback,
  WriteMemoryCallback as CoreWriteMemoryCallback,
  PCHookCallback as CorePCHookCallback,
  System as CoreSystem,
  SystemConfig as CoreSystemConfig,
  CpuRegisters as CoreCpuRegisters,
  HookCallback as CoreHookCallback,
  Tracer as CoreTracer,
  TraceConfig as CoreTraceConfig,
  SymbolMap as CoreSymbolMap,
  MemoryAccessCallback as CoreMemoryAccessCallback,
  MemoryAccessEvent as CoreMemoryAccessEvent,
  MemoryLayout as CoreMemoryLayout,
  MemoryRegion as CoreMemoryRegion,
  MirrorRegion as CoreMirrorRegion,
} from './core/index.js';

declare module 'musashi-wasm' {
  export { createSystem, M68kRegister } from './core/index.js';
  export type System = CoreSystem;
  export type SystemConfig = CoreSystemConfig;
  export type CpuRegisters = CoreCpuRegisters;
  export type HookCallback = CoreHookCallback;
  export type Tracer = CoreTracer;
  export type TraceConfig = CoreTraceConfig;
  export type SymbolMap = CoreSymbolMap;
  export type MemoryAccessCallback = CoreMemoryAccessCallback;
  export type MemoryAccessEvent = CoreMemoryAccessEvent;
  export type MemoryLayout = CoreMemoryLayout;
  export type MemoryRegion = CoreMemoryRegion;
  export type MirrorRegion = CoreMirrorRegion;

  export interface MusashiModule {
    _m68k_init(): void;
    _m68k_execute(cycles: number): number;
    _m68k_pulse_reset(): void;
    _m68k_cycles_run(): number;
    _m68k_get_reg(context: number | null, reg: number): number;
    _m68k_set_reg(reg: number, value: number): void;
    _add_region(base: number, size: number, dataPtr: number): void;
    _clear_regions(): void;
    _set_read_mem_func(ptr: number): void;
    _set_write_mem_func(ptr: number): void;
    _set_pc_hook_func(ptr: number): void;
    _enable_printf_logging(enable: number): void;
    _malloc(size: number): number;
    _free(ptr: number): void;
    HEAPU8: Uint8Array;
    addFunction(func: Function, signature: string): number;
    removeFunction(ptr: number): void;
    getValue(ptr: number, type: string): number;
    allocateUTF8(str: string): number;
  }

  export type M68kRegister = CoreM68kRegister;
  export type ReadMemoryCallback = CoreReadMemoryCallback;
  export type WriteMemoryCallback = CoreWriteMemoryCallback;
  export type PCHookCallback = CorePCHookCallback;

  export class Musashi {
    constructor();
    
    /**
     * Initialize the M68k emulator. Must be called before any other operations.
     */
    init(): Promise<void>;
    
    /**
     * Execute CPU instructions for the specified number of cycles.
     * @param cycles Number of cycles to execute
     * @returns Number of cycles actually executed
     */
    execute(cycles: number): number;
    
    /**
     * Pulse the RESET line of the CPU.
     */
    pulseReset(): void;
    
    /**
     * Get the number of cycles executed so far.
     * @returns Number of cycles run
     */
    cyclesRun(): number;
    
    /**
     * Get the value of a CPU register.
     * @param reg Register identifier from M68kRegister enum
     * @returns Register value
     */
    getReg(reg: M68kRegister): number;
    
    /**
     * Set the value of a CPU register.
     * @param reg Register identifier from M68kRegister enum
     * @param value Value to set
     */
    setReg(reg: M68kRegister, value: number): void;
    
    /**
     * Add a memory region to the emulator.
     * @param base Base address of the region
     * @param size Size of the region in bytes
     * @param dataPtr Pointer to the memory data in WASM heap
     */
    addRegion(base: number, size: number, dataPtr: number): void;
    
    /**
     * Clear all memory regions.
     */
    clearRegions(): void;
    
    /**
     * Set the memory read callback function.
     * @param func Callback function that reads memory
     * @returns Function pointer that can be used with removeFunction
     */
    setReadMemFunc(func: ReadMemoryCallback): number;
    
    /**
     * Set the memory write callback function.
     * @param func Callback function that writes memory
     * @returns Function pointer that can be used with removeFunction
     */
    setWriteMemFunc(func: WriteMemoryCallback): number;
    
    /**
     * Set the PC hook callback function.
     * @param func Callback function called on each instruction
     * @returns Function pointer that can be used with removeFunction
     */
    setPCHookFunc(func: PCHookCallback): number;
    
    /**
     * Remove a previously registered callback function.
     * @param ptr Function pointer returned by set*Func methods
     */
    removeFunction(ptr: number): void;
    
    /**
     * Allocate memory in the WASM heap.
     * @param size Number of bytes to allocate
     * @returns Pointer to allocated memory
     */
    allocateMemory(size: number): number;
    
    /**
     * Free memory in the WASM heap.
     * @param ptr Pointer to memory to free
     */
    freeMemory(ptr: number): void;
    
    /**
     * Write data to WASM heap memory.
     * @param ptr Pointer to destination in WASM heap
     * @param data Data to write
     */
    writeMemory(ptr: number, data: Uint8Array): void;
    
    /**
     * Read data from WASM heap memory.
     * @param ptr Pointer to source in WASM heap
     * @param size Number of bytes to read
     * @returns Data read from memory
     */
    readMemory(ptr: number, size: number): Uint8Array;
    
    /**
     * Enable or disable printf logging.
     * @param enable True to enable, false to disable
     */
    enablePrintfLogging(enable: boolean): void;
  }

  export default Musashi;
}

declare module 'musashi-wasm/perfetto' {
  import { Musashi, M68kRegister, ReadMemoryCallback, WriteMemoryCallback, PCHookCallback } from 'musashi-wasm';

  export class MusashiPerfetto extends Musashi {
    constructor();
    
    /**
     * Initialize the M68k emulator with Perfetto tracing support.
     * @param processName Name to use for the process in traces (default: 'Musashi')
     */
    init(processName?: string): Promise<void>;
    
    /**
     * Enable or disable flow tracing.
     * @param enable True to enable, false to disable
     */
    enableFlowTracing(enable: boolean): void;
    
    /**
     * Enable or disable instruction tracing.
     * @param enable True to enable, false to disable
     */
    enableInstructionTracing(enable: boolean): void;
    
    /**
     * Enable or disable memory access tracing.
     * @param enable True to enable, false to disable
     */
    enableMemoryTracing(enable: boolean): void;
    
    /**
     * Enable or disable interrupt tracing.
     * @param enable True to enable, false to disable
     */
    enableInterruptTracing(enable: boolean): void;
    
    /**
     * Export the current trace data as a Uint8Array.
     * @returns Trace data in Perfetto format, or null if no trace data
     */
    exportTrace(): Promise<Uint8Array | null>;
    
    /**
     * Save the current trace data to a file.
     * @param filename Path to save the trace file
     * @returns True if saved successfully, false otherwise
     */
    saveTrace(filename: string): Promise<boolean>;
  }

  export { M68kRegister, ReadMemoryCallback, WriteMemoryCallback, PCHookCallback };
  export default MusashiPerfetto;
}
