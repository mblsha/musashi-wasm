// Shared test utilities for @m68k/core
import type { System } from './types.js';

export enum BreakReason {
  None = 0,
  Trace = 1,
  InstrHook = 2,
  JsHook = 3,
  Sentinel = 4,
}

// Narrow access to internal Musashi debug hooks without leaking `any`.
interface MusashiDebug {
  getLastBreakReason(): number;
  resetLastBreakReason(): void;
}
interface HasMusashi {
  _musashi?: MusashiDebug;
}

export function getLastBreakReasonFrom(system: System): number {
  const s = system as unknown as HasMusashi;
  return s._musashi?.getLastBreakReason?.() ?? 0;
}

export function resetLastBreakReasonOn(system: System): void {
  const s = system as unknown as HasMusashi;
  s._musashi?.resetLastBreakReason?.();
}
