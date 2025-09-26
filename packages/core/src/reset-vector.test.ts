import { createSystem } from './index.js';
import type { System } from './types.js';

describe('@m68k/core reset vector handling', () => {
  let system: System | undefined;

  afterEach(() => {
    system?.cleanup();
    system = undefined;
  });

  it('respects ROM-provided PC while backfilling missing SP', async () => {
    const rom = new Uint8Array(0x1000);

    // Leave SSP zero so the wrapper backfills the legacy default, but point the
    // reset PC at 0x0200 so we can observe the CPU fetching from that address.
    rom[0x0004] = 0x00;
    rom[0x0005] = 0x00;
    rom[0x0006] = 0x02;
    rom[0x0007] = 0x00;

    // Program at 0x0200: NOP; RTS (both 2-byte instructions)
    rom[0x0200] = 0x4e; rom[0x0201] = 0x71; // NOP
    rom[0x0202] = 0x4e; rom[0x0203] = 0x75; // RTS

    system = await createSystem({ rom, ramSize: 0x20000 });

    const regs0 = system.getRegisters();
    expect(regs0.pc >>> 0).toBe(0x200);
    expect(regs0.sp >>> 0).toBe(0x00108000); // default backfill for zero SSP

    const step1 = system.step();
    expect(step1.startPc >>> 0).toBe(0x200);
    expect(step1.endPc >>> 0).toBe(0x202);

    const regs1 = system.getRegisters();
    expect(regs1.pc >>> 0).toBe(0x202);
  });

  it('rejects odd reset vectors', async () => {
    const rom = new Uint8Array(0x1000);

    rom[0x0000] = 0x00;
    rom[0x0001] = 0x01;
    rom[0x0002] = 0x00;
    rom[0x0003] = 0x00;

    rom[0x0004] = 0x00;
    rom[0x0005] = 0x00;
    rom[0x0006] = 0x02;
    rom[0x0007] = 0x01; // odd PC

    await expect(createSystem({ rom, ramSize: 0x20000 })).rejects.toThrow(
      /Reset vector PC must be even/
    );
  });
});
