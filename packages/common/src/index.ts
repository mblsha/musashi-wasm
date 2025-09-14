// Canonical M68k register enum, aligned with m68k.h (m68k_register_t)
// Keep values synchronized with the C header to avoid drift.
export enum M68kRegister {
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
  PREF_ADDR = 28,
  PREF_DATA = 29,
  IR = 30,
  CPU_TYPE = 31,
}

// Shared callback types for WASM bridges and wrappers
export type ReadMemoryCallback = (address: number) => number;
export type WriteMemoryCallback = (address: number, value: number) => void;
export type PCHookCallback = (address: number) => number;
