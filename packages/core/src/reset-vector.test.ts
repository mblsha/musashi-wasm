import { createSystem } from './index.js';
import type { System, CpuRegisters } from './types.js';

const writeWord = (rom: Uint8Array, addr: number, value: number) => {
  rom[addr] = (value >>> 8) & 0xff;
  rom[addr + 1] = value & 0xff;
};

const writeLong = (rom: Uint8Array, addr: number, value: number) => {
  rom[addr + 0] = (value >>> 24) & 0xff;
  rom[addr + 1] = (value >>> 16) & 0xff;
  rom[addr + 2] = (value >>> 8) & 0xff;
  rom[addr + 3] = value & 0xff;
};

const moveImmediateLongOpcode = (dReg: number): number => 0x203c | ((dReg & 7) << 9);

const dataRegisterName = (dReg: number): keyof CpuRegisters => `d${dReg}` as keyof CpuRegisters;

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
    writeLong(rom, 0x0004, 0x00000200);

    // Program at 0x0200: NOP; RTS (both 2-byte instructions)
    writeWord(rom, 0x0200, 0x4e71); // NOP
    writeWord(rom, 0x0202, 0x4e75); // RTS

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

    writeLong(rom, 0x0000, 0x00010000);
    writeLong(rom, 0x0004, 0x00000201); // odd PC

    await expect(createSystem({ rom, ramSize: 0x20000 })).rejects.toThrow(
      /Reset vector PC must be even/
    );
  });

  it.each([
    ['0x0300 -> D0', 0x0300, 0, 0xcafebabe],
    ['0x0480 -> D2', 0x0480, 2, 0xdeadbeef],
  ])('starts execution at custom vector %s', async (_label, targetPc, dReg, value) => {
    const rom = new Uint8Array(0x4000);

    writeLong(rom, 0x0000, 0x00002000); // SSP inside RAM window
    writeLong(rom, 0x0004, targetPc);

    // Guard the vector entry with an RTS immediately prior to it, ensuring
    // straight-line execution cannot fall into the handler without the vector.
    writeWord(rom, targetPc - 2, 0x4e75);

    const opcode = moveImmediateLongOpcode(dReg);
    writeWord(rom, targetPc, opcode);
    writeLong(rom, targetPc + 2, value >>> 0);
    writeWord(rom, targetPc + 6, 0x4e75); // return immediately

    system = await createSystem({ rom, ramSize: 0x40000 });

    const regs0 = system.getRegisters();
    expect(regs0.pc >>> 0).toBe(targetPc);

    const registerName = dataRegisterName(dReg);
    system.setRegister(registerName, 0);

    const step1 = system.step();
    expect(step1.startPc >>> 0).toBe(targetPc);
    expect(step1.endPc >>> 0).toBe(targetPc + 6);

    const regs1 = system.getRegisters();
    expect(regs1[registerName] >>> 0).toBe(value >>> 0);
    expect(regs1.pc >>> 0).toBe(targetPc + 6);
  });
});
