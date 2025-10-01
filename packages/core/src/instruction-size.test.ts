import { createSystem } from './index';
import type { System } from './types.js';

describe('getInstructionSize', () => {
  let sys: System | undefined;

  afterEach(() => {
    sys?.cleanup();
    sys = undefined;
  });

  it('returns correct sizes for common instructions', async () => {
    const rom = new Uint8Array(0x4000);
    sys = await createSystem({ rom, ramSize: 0x20000 });
    const system = sys;

    if (!system) {
      throw new Error('Failed to initialize system for instruction size tests');
    }

    // NOP at 0x0600 (0x4E71) -> 2 bytes
    system.write(0x600, 1, 0x4e);
    system.write(0x601, 1, 0x71);
    expect(system.getInstructionSize(0x600)).toBe(2);

    // RTS at 0x0602 (0x4E75) -> 2 bytes
    system.write(0x602, 1, 0x4e);
    system.write(0x603, 1, 0x75);
    expect(system.getInstructionSize(0x602)).toBe(2);

    // MOVEQ #$7f, D3 at 0x0800 (0x73 0x7f) -> 2 bytes
    system.write(0x800, 1, 0x73);
    system.write(0x801, 1, 0x7f);
    expect(system.getInstructionSize(0x800)).toBe(2);

    // LINK A6,#-$8 at 0x0810 (0x4e 0x56 0xff 0xf8) -> 4 bytes
    system.write(0x810, 1, 0x4e);
    system.write(0x811, 1, 0x56);
    system.write(0x812, 1, 0xff);
    system.write(0x813, 1, 0xf8);
    expect(system.getInstructionSize(0x810)).toBe(4);

    // ADDI.L #$1, D1 at 0x0860 -> 6 bytes
    const addi = [0x06, 0x81, 0x00, 0x00, 0x00, 0x01];
    for (let i = 0; i < addi.length; i++) system.write(0x860 + i, 1, addi[i]);
    expect(system.getInstructionSize(0x860)).toBe(6);

    // JSR $00000600 at 0x0900 (absolute long) -> 6 bytes
    const jsrAbsLong = [0x4e, 0xb9, 0x00, 0x00, 0x06, 0x00];
    for (let i = 0; i < jsrAbsLong.length; i++) system.write(0x900 + i, 1, jsrAbsLong[i]);
    expect(system.getInstructionSize(0x900)).toBe(6);

    // DBRA D0,$87e at 0x0A00 -> 4 bytes
    const dbra = [0x51, 0xc8, 0xff, 0xfe];
    for (let i = 0; i < dbra.length; i++) system.write(0xa00 + i, 1, dbra[i]);
    expect(system.getInstructionSize(0xa00)).toBe(4);
  });
});
