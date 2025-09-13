// Shared test utilities for @m68k/core

export enum BreakReason {
  None = 0,
  Trace = 1,
  InstrHook = 2,
  JsHook = 3,
  Sentinel = 4,
}

export function getLastBreakReasonFrom(system: any): number {
  return system?._musashi?.getLastBreakReason?.() ?? 0;
}

export function resetLastBreakReasonOn(system: any): void {
  system?._musashi?.resetLastBreakReason?.();
}

