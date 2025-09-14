import { createSystem } from './index';

describe('disassembly helpers', () => {
  it('disassembles a NOP at 0x400', async () => {
    const rom = new Uint8Array(0x2000);
    const sys = await createSystem({ rom, ramSize: 0x10000 });

    // Place a NOP (0x4E71) at the reset PC (0x00000400)
    sys.write(0x400, 1, 0x4e);
    sys.write(0x401, 1, 0x71);

    const line = sys.disassemble(0x400);
    expect(line).toBe('nop');
  });

  it('disassembles a short sequence', async () => {
    const rom = new Uint8Array(0x2000);
    const sys = await createSystem({ rom, ramSize: 0x10000 });

    // Sequence: NOP (0x4E71), RTS (0x4E75)
    sys.write(0x600, 1, 0x4e);
    sys.write(0x601, 1, 0x71);
    sys.write(0x602, 1, 0x4e);
    sys.write(0x603, 1, 0x75);

    expect(sys.disassemble(0x600)).toBe('nop');
    expect(sys.disassemble(0x602)).toBe('rts');
  });

  it('disassembles complex instructions and formats text lines', async () => {
    const rom = new Uint8Array(0x4000);
    const sys = await createSystem({ rom, ramSize: 0x20000 });

    const cases: Array<{ addr: number; bytes: number[]; expect: string }> = [
      // MOVEQ #$7f,D3 encoding: 0x767f (0111 ddd 0 iiiiiiii)
      { addr: 0x0800, bytes: [0x76, 0x7f], expect: 'moveq   #$7f, D3' },
      { addr: 0x0810, bytes: [0x4e, 0x56, 0xff, 0xf8], expect: 'link    A6, #-$8' },
      { addr: 0x0820, bytes: [0x4e, 0x5e], expect: 'unlk    A6' },
      { addr: 0x0830, bytes: [0x41, 0xf9, 0x00, 0x12, 0x34, 0x56], expect: 'lea     $123456.l, A0' },
      { addr: 0x0840, bytes: [0x4e, 0x91], expect: 'jsr     (A1)' },
      // BSR disp8 is relative to PC+2, so from 0x0850 with +6 -> 0x0858
      { addr: 0x0850, bytes: [0x61, 0x06], expect: 'bsr     $858' },
      { addr: 0x0860, bytes: [0x06, 0x81, 0x00, 0x00, 0x00, 0x01], expect: 'addi.l  #$1, D1' },
      { addr: 0x0870, bytes: [0x48, 0x57], expect: 'pea     (A7)' },
      { addr: 0x0880, bytes: [0x51, 0xc8, 0xff, 0xfe], expect: 'dbra    D0, $87e' },
    ];

    for (const tc of cases) {
      for (let i = 0; i < tc.bytes.length; i++) sys.write(tc.addr + i, 1, tc.bytes[i]);
      const line = sys.disassemble(tc.addr);
      expect(line).toBe(tc.expect);
    }
  });
});
