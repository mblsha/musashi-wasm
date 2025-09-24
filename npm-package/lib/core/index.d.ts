import type { System, SystemConfig, CpuRegisters, HookCallback, Tracer, TraceConfig, SymbolMap } from './types.js';
export type { System, SystemConfig, CpuRegisters, HookCallback, Tracer, TraceConfig, SymbolMap, };
export { M68kRegister } from '@m68k/common';
/**
 * Asynchronously creates and initializes a new M68k system instance.
 * @param config The system configuration.
 */
export declare function createSystem(config: SystemConfig): Promise<System>;
//# sourceMappingURL=index.d.ts.map