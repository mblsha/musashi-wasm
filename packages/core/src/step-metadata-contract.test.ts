import { createSystem } from './index.js';
import type { System } from './types.js';

describe('step() metadata contract', () => {
  let system: System;

  beforeEach(async () => {
    // Minimal ROM with reset vectors and a tiny program at 0x400
    const rom = new Uint8Array(0x1000);
    // SSP=0x00010000
    rom[0] = 0x00; rom[1] = 0x01; rom[2] = 0x00; rom[3] = 0x00;
    // PC = 0x00000400
    rom[4] = 0x00; rom[5] = 0x00; rom[6] = 0x04; rom[7] = 0x00;
    // Program at 0x400
    // MOVE.L #$12345678, D0  (6)
    rom[0x400] = 0x20; rom[0x401] = 0x3c; rom[0x402] = 0x12; rom[0x403] = 0x34; rom[0x404] = 0x56; rom[0x405] = 0x78;
    // MOVE.L D0,(A0)         (2)
    rom[0x406] = 0x20; rom[0x407] = 0x80;
    // ADD.L #1,D1            (6)
    rom[0x408] = 0x06; rom[0x409] = 0x81; rom[0x40a] = 0x00; rom[0x40b] = 0x00; rom[0x40c] = 0x00; rom[0x40d] = 0x01;

    system = await createSystem({ rom, ramSize: 0x1000 });
  });

  it('endPc equals startPc + getInstructionSize(startPc)', async () => {
    // Step 1
    let start = system.getRegisters().pc >>> 0;
    let size = system.getInstructionSize(start) >>> 0;
    const s1 = await system.step();
    expect(s1.startPc >>> 0).toBe(start);
    expect(s1.endPc >>> 0).toBe((start + size) >>> 0);

    // Prepare for next: A0 used by second instruction
    system.setRegister('a0', 0x100000);

    // Step 2
    start = system.getRegisters().pc >>> 0;
    size = system.getInstructionSize(start) >>> 0;
    const s2 = await system.step();
    expect(s2.startPc >>> 0).toBe(start);
    expect(s2.endPc >>> 0).toBe((start + size) >>> 0);

    // Step 3
    start = system.getRegisters().pc >>> 0;
    size = system.getInstructionSize(start) >>> 0;
    const s3 = await system.step();
    expect(s3.startPc >>> 0).toBe(start);
    expect(s3.endPc >>> 0).toBe((start + size) >>> 0);
  });
});

