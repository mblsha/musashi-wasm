import type { System } from './types.js';
export declare enum BreakReason {
    None = 0,
    Trace = 1,
    InstrHook = 2,
    JsHook = 3,
    Sentinel = 4
}
export declare function getLastBreakReasonFrom(system: System): number;
export declare function resetLastBreakReasonOn(system: System): void;
//# sourceMappingURL=test-utils.d.ts.map