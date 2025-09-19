import { createSystem } from './index.js';

describe('@m68k/core step() control flow', () => {
  const STACK_BASE = 0x0010f300;
  const ENTRY_PC = 0x0400;
  const JSR_PC = 0x0404;
  const RETURN_PC = 0x040a;
  const TARGET_PC = 0x05dc1c;

  const writeBytes = (buffer: Uint8Array, addr: number, bytes: number[]) => {
    buffer.set(bytes, addr);
  };

  it('enters the jsr target and records the return address on stack', async () => {
    const rom = new Uint8Array(0x200000);
    const ramSize = 0x100000;

    // Reset vectors
    writeBytes(rom, 0, [
      (STACK_BASE >>> 24) & 0xff,
      (STACK_BASE >>> 16) & 0xff,
      (STACK_BASE >>> 8) & 0xff,
      STACK_BASE & 0xff
    ]);
    writeBytes(rom, 4, [0x00, 0x00, 0x04, 0x00]); // entry PC

    // movem.l D0-D7/A0-A6, -(A7)
    writeBytes(rom, ENTRY_PC, [0x48, 0xe7, 0xff, 0xfe]);
    // jsr $05dc1c.l
    writeBytes(rom, JSR_PC, [0x4e, 0xb9, 0x00, 0x05, 0xdc, 0x1c]);
    // return trampoline: rts
    writeBytes(rom, RETURN_PC, [0x4e, 0x75]);

    // Callee body
    writeBytes(rom, TARGET_PC, [0x30, 0x3c, 0x00, 0x9c]);
    writeBytes(rom, TARGET_PC + 4, [0x21, 0xbc, 0xff, 0xff, 0xff, 0xff]);
    writeBytes(rom, TARGET_PC + 10, [0x4e, 0x75]);

    const system = await createSystem({ rom, ramSize });
    try {
      system.reset();
      system.setRegister('sp', STACK_BASE);
      system.setRegister('a0', 0x00100a80);
      system.setRegister('d0', 0x009c);
      system.setRegister('sr', 0x2704);

      const step1 = system.step();
      expect(step1.startPc >>> 0).toBe(ENTRY_PC);
      expect(step1.endPc >>> 0).toBe(JSR_PC);

      const step2 = system.step();
      const regsAfterCall = system.getRegisters();

      expect(step2.startPc >>> 0).toBe(JSR_PC);
      expect(step2.endPc >>> 0).toBe(TARGET_PC);
      expect(regsAfterCall.pc >>> 0).toBe(TARGET_PC);

      const expectedSp = STACK_BASE - (15 * 4) - 4; // MOVEM saves 15 longs, JSR pushes return
      expect(regsAfterCall.sp >>> 0).toBe(expectedSp);

      const stackBytes = Array.from(
        system.readBytes(expectedSp >>> 0, 4),
        (b) => b & 0xff
      );
      expect(stackBytes).toEqual([0x00, 0x00, 0x04, 0x0a]);

      const step3 = system.step();
      expect(step3.startPc >>> 0).toBe(TARGET_PC);
      expect(regsAfterCall.pc >>> 0).toBe(TARGET_PC);
      const regsAfterStep3 = system.getRegisters();
      expect(regsAfterStep3.pc >>> 0).toBe(TARGET_PC + 4);
    } finally {
      system.cleanup();
    }
  });
});
