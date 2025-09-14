// Single-instruction stepping tests using real WASM
import { createSystem } from './index.js';
import type { System } from './types.js';

describe('@m68k/core step()', () => {
  let system: System;

  beforeEach(async () => {
    // Minimal ROM with reset vectors and a tiny program at 0x400
    const rom = new Uint8Array(0x1000);

    // Initial SSP = 0x00010000
    rom[0] = 0x00;
    rom[1] = 0x01;
    rom[2] = 0x00;
    rom[3] = 0x00;
    // Initial PC = 0x00000400
    rom[4] = 0x00;
    rom[5] = 0x00;
    rom[6] = 0x04;
    rom[7] = 0x00;

    // Program at 0x400:
    // 0x400: MOVE.L #$12345678, D0   (6 bytes)
    rom[0x400] = 0x20; rom[0x401] = 0x3c;
    rom[0x402] = 0x12; rom[0x403] = 0x34; rom[0x404] = 0x56; rom[0x405] = 0x78;
    // 0x406: MOVE.L D0, (A0)         (2 bytes)
    rom[0x406] = 0x20; rom[0x407] = 0x80;
    // 0x408: ADD.L #1, D1            (6 bytes)
    rom[0x408] = 0x06; rom[0x409] = 0x81; rom[0x40a] = 0x00; rom[0x40b] = 0x00; rom[0x40c] = 0x00; rom[0x40d] = 0x01;
    // 0x40E: RTS                     (2 bytes)
    rom[0x40e] = 0x4e; rom[0x40f] = 0x75;

    system = await createSystem({ rom, ramSize: 0x1000 });
  });

  it('executes exactly one instruction and advances PC', async () => {
    const regs0 = system.getRegisters();
    expect(regs0.pc >>> 0).toBe(0x400);

    const s1 = await system.step();
    expect(s1.cycles).toBeGreaterThan(0);
    expect(s1.startPc >>> 0).toBe(0x400);
    expect(s1.endPc >>> 0).toBe(0x406);
    const regs1 = system.getRegisters();
    expect(regs1.pc >>> 0).toBe(0x406); // 6-byte immediate MOVE
    expect(regs1.d0 >>> 0).toBe(0x12345678);

    // Ensure next step advances by 2 bytes for MOVE.L D0,(A0)
    system.setRegister('a0', 0x100000);
    const s2 = await system.step();
    expect(s2.cycles).toBeGreaterThan(0);
    expect(s2.startPc >>> 0).toBe(0x406);
    expect(s2.endPc >>> 0).toBe(0x408);
    const regs2 = system.getRegisters();
    expect(regs2.pc >>> 0).toBe(0x408);

    // Next step should skip 6-byte ADD.L #1,D1
    const s3 = await system.step();
    expect(s3.cycles).toBeGreaterThan(0);
    expect(s3.startPc >>> 0).toBe(0x408);
    expect(s3.endPc >>> 0).toBe(0x40e);
    const regs3 = system.getRegisters();
    expect(regs3.pc >>> 0).toBe(0x40e);
  });
});
