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

    // Execute two instructions
    await sys.step(); // @ 0x416
    await sys.step(); // @ 0x41c

    offR?.();
    offW?.();

    // Focus on read-path verification for absolute long; incidental writes from
    // core internals are not relevant here.
    // Find the two absolute-long reads by address and size
    const ABS = 0x00106e80 >>> 0;
    const absReads = reads.filter(r => (r.addr >>> 0) === ABS && r.size === 4);
    expect(absReads.length).toBeGreaterThanOrEqual(2);
    const r0 = absReads[0];
    const r1 = absReads[1];
    // Initial memory is zeroed, so values are 0
    expect(r0.value >>> 0).toBe(0);
    expect(r1.value >>> 0).toBe(0);

    // PCs should match instruction addresses
    expect(r0.pc >>> 0).toBe(0x416);
    expect(r1.pc >>> 0).toBe(0x41c);

    // Sanity print on failure
    if (reads.length < 2) {
      // eslint-disable-next-line no-console
      console.log('Reads:', reads.map((e) => ({ ...e, pc: hex(e.pc), addr: hex(e.addr) })));
    }
  });
});
