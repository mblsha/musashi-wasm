type EmscriptenBuffer = number;
type EmscriptenFunction = number;
import { M68kRegister } from '@m68k/common';
import type { MemoryLayout } from './types.js';
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
    HEAPU8: Uint8Array;
    HEAP32: Int32Array;
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
    _m68k_set_trace_mem_callback?(f: EmscriptenFunction): void;
    _m68k_trace_set_mem_enabled?(enable: number): void;
    _m68k_trace_add_mem_region?(start: number, end: number): number;
    _m68k_trace_clear_mem_regions?(): void;
    _set_read8_callback?(f: EmscriptenFunction): void;
    _set_write8_callback?(f: EmscriptenFunction): void;
    _set_probe_callback?(f: EmscriptenFunction): void;
    _m68k_disassemble?(outBuf: EmscriptenBuffer, pc: number, cpu_type: number): number;
    _m68k_call_until_js_stop?(entry_pc: number, timeslice: number): number;
    _m68k_get_last_break_reason?(): number;
    _m68k_reset_last_break_reason?(): void;
    setValue?(ptr: number, value: number, type: string): void;
    getValue?(ptr: number, type: string): number;
}
interface SystemBridge {
    read(address: number, size: 1 | 2 | 4): number;
    write(address: number, size: 1 | 2 | 4, value: number): void;
    _handlePCHook(pc: number): boolean;
    ram: Uint8Array;
    _handleMemoryRead?(addr: number, size: 1 | 2 | 4, value: number, pc: number): void;
    _handleMemoryWrite?(addr: number, size: 1 | 2 | 4, value: number, pc: number): void;
}
export declare class MusashiWrapper {
    private _module;
    private _system;
    private _memory;
    private _ramWindows;
    private _readFunc;
    private _writeFunc;
    private _probeFunc;
    private _memTraceFunc;
    private _memTraceActive;
    private readonly CPU_68000;
    constructor(module: MusashiEmscriptenModule);
    private applyDefaultMemoryMapping;
    private getActiveMemoryLayout;
    init(system: SystemBridge, rom: Uint8Array, ram: Uint8Array, memoryLayout?: MemoryLayout): void;
    private write32BE;
    cleanup(): void;
    readHandler(address: number, size: 1 | 2 | 4): number;
    writeHandler(address: number, size: 1 | 2 | 4, value: number): void;
    pcHookHandler(pc: number): number;
    execute(cycles: number): number;
    step(): number;
    private requireExport;
    call(address: number): number;
    getLastBreakReason(): number;
    resetLastBreakReason(): void;
    pulse_reset(): void;
    get_reg(index: M68kRegister): number;
    set_reg(index: M68kRegister, value: number): void;
    add_pc_hook_addr(addr: number): void;
    private findRamWindowForAddress;
    private isAccessWithinMemory;
    read_memory(address: number, size: 1 | 2 | 4): number;
    write_memory(address: number, size: 1 | 2 | 4, value: number): void;
    setMemoryTraceEnabled(enable: boolean): void;
    /**
     * Disassembles a single instruction at the given address.
     * Returns null if the underlying module does not expose the disassembler.
     */
    disassemble(address: number): {
        text: string;
        size: number;
    } | null;
    /**
     * Convenience helper to disassemble a short sequence starting at address.
     */
    disassembleSequence(address: number, count: number): Array<{
        pc: number;
        text: string;
        size: number;
    }>;
    isPerfettoAvailable(): boolean;
    private withHeapString;
    perfettoInit(processName: string): number;
    perfettoDestroy(): void;
    perfettoCleanupSlices(): void;
    perfettoEnableFlow(enable: boolean): void;
    perfettoEnableMemory(enable: boolean): void;
    perfettoEnableInstructions(enable: boolean): void;
    traceEnable(enable: boolean): void;
    perfettoIsInitialized(): boolean;
    private _registerSymbol;
    registerFunctionName(address: number, name: string): void;
    registerMemoryName(address: number, name: string): void;
    perfettoExportTrace(): Uint8Array | null;
}
export declare function getModule(): Promise<MusashiWrapper>;
export {};
//# sourceMappingURL=musashi-wrapper.d.ts.map