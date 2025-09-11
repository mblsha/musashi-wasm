import { createSystem } from './index';

describe('disassembly helpers', () => {
  it('disassembles a NOP at 0x400', async () => {
    const rom = new Uint8Array(0x2000);
    const sys = await createSystem({ rom, ramSize: 0x10000 });

    // Place a NOP (0x4E71) at the reset PC (0x00000400)
    sys.write(0x400, 1, 0x4e);
    sys.write(0x401, 1, 0x71);

    const d = sys.disassemble(0x400);
    expect(d).not.toBeNull();
    expect(d!.size).toBe(2);
    expect(d!.text.toLowerCase()).toContain('nop');
  });

  it('disassembles a short sequence', async () => {
    const rom = new Uint8Array(0x2000);
    const sys = await createSystem({ rom, ramSize: 0x10000 });

    // Sequence: NOP (0x4E71), RTS (0x4E75)
    sys.write(0x600, 1, 0x4e);
    sys.write(0x601, 1, 0x71);
    sys.write(0x602, 1, 0x4e);
    sys.write(0x603, 1, 0x75);

    const seq = sys.disassembleSequence(0x600, 2);
    expect(seq.length).toBe(2);
    expect(seq[0].pc).toBe(0x600);
    expect(seq[0].size).toBe(2);
    expect(seq[0].text.toLowerCase()).toContain('nop');
    expect(seq[1].pc).toBe(0x602);
    expect(seq[1].size).toBe(2);
    expect(seq[1].text.toLowerCase()).toContain('rts');
  });
});

