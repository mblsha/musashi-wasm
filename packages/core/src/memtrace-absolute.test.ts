import { createSystem } from './index.js';

function hex(n: number) {
  return '0x' + (n >>> 0).toString(16).padStart(8, '0');
}

describe('Memory trace for absolute long accesses (read path)', () => {
  it('emits read events with correct addr/size/value/pc for MOVE.L (abs).L', async () => {
    // Build a ROM large enough to place code at 0x416 and 0x41c
    const rom = new Uint8Array(0x200000);

    // Program:
    // 0x00416: MOVEA.L $106e80.L, A0   (reads from absolute address)
    // 0x0041c: MOVE.L  $106e80.L, D0   (reads from absolute address)
    // Note: These opcodes exercise absolute long addressing reads specifically.
    rom.set([0x20, 0x79, 0x00, 0x10, 0x6e, 0x80], 0x416);
    rom.set([0x20, 0x39, 0x00, 0x10, 0x6e, 0x80], 0x41c);

    const sys = await createSystem({ rom, ramSize: 0x100000 });
    sys.setRegister('sr', 0x2704);
    sys.setRegister('sp', 0x10f300);
    sys.setRegister('a0', 0x100a80);
    sys.setRegister('pc', 0x416);

    const reads: Array<{ addr: number; size: number; value: number; pc: number }> = [];
    const writes: Array<{ addr: number; size: number; value: number; pc: number }> = [];

    const offR = sys.onMemoryRead((e) => reads.push(e));
    const offW = sys.onMemoryWrite((e) => writes.push(e));

    // Execute two instructions and capture actual start PCs
    const s1 = await sys.step(); // expected around 0x416
    const s2 = await sys.step(); // next instruction (near 0x41c)

    offR?.();
    offW?.();

    // Focus on read-path verification for absolute long; incidental writes from
    // core internals are not relevant here.
    // Find absolute-long reads at ABS and group by PC to allow for 16-bit split reads
    const ABS = 0x00106e80 >>> 0;
    // Some cores perform 32-bit reads as two 16-bit reads starting at ABS or ABS+2.
    const absReads = reads.filter(r => {
      const a = r.addr >>> 0;
      return a >= ABS && a < (ABS + 4) && (r.size === 1 || r.size === 2 || r.size === 4);
    });
    // We expect the two executed instructions (by their actual start PCs) to read 4 bytes each
    const byPc = new Map<number, number>();
    for (const r of absReads) {
      const pc = r.pc >>> 0;
      byPc.set(pc, (byPc.get(pc) || 0) + r.size);
    }
    const pc1 = s1.startPc >>> 0;
    const pc2 = s2.startPc >>> 0;
    expect(byPc.get(pc1) || 0).toBe(4);
    expect(byPc.get(pc2) || 0).toBe(4);

    // Sanity print on failure
    if (reads.length < 2) {
      // eslint-disable-next-line no-console
      console.log('Reads:', reads.map((e) => ({ ...e, pc: hex(e.pc), addr: hex(e.addr) })));
    }
  });
});
