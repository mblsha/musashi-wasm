import { createSystem } from './index.js';
import type { MemoryAccessEvent } from './types.js';

describe('system.step exception handling', () => {
  it('requires an additional step() to execute the trap handler', async () => {
    const rom = new Uint8Array(0x200000);
    const ramSize = 0x100000;
    const handlerAddr = 0x400;

    const view32 = new DataView(rom.buffer);
    view32.setUint32(0x0, 0x00100000); // initial SSP somewhere in RAM
    view32.setUint32(0x4, handlerAddr); // initial PC (unused but valid)
    view32.setUint32(0x2c, handlerAddr); // vector 11 (Line 1111)

    const handler = Uint8Array.from([
      0x23, 0xFC, 0xFF, 0xFF, 0xFF, 0xFF, // MOVE.L #$FFFFFFFF, (absolute)
      0x00, 0x10, 0x0B, 0x1C,             // Address $00100B1C
      0x4E, 0x73                          // RTE
    ]);
    rom.set(handler, handlerAddr);

    const system = await createSystem({ rom, ramSize });

    system.setRegister('pc', 0x5DC20);
    system.setRegister('a0', 0x100A80);
    system.setRegister('d0', 156);

    const initialSp = system.getRegisters().sp >>> 0;

    const flineBytes = [0xF9, 0x41, 0x10, 0x00, 0xB0, 0x6E];
    for (let i = 0; i < flineBytes.length; i++) {
      system.write(0x5DC20 + i, 1, flineBytes[i]);
    }

    const memoryWrites: MemoryAccessEvent[] = [];
    system.onMemoryWrite((event: MemoryAccessEvent) => memoryWrites.push(event));

    const expectedHandlerSize = system.getInstructionSize(handlerAddr);

    const firstStep = system.step();
    const regsAfterFirst = system.getRegisters();

    let trapWrite = memoryWrites.find(
      (w) => w.addr === 0x00100B1C && w.size === 4 && w.value === 0xFFFFFFFF
    );

    expect(firstStep.startPc).toBe(0x5DC20);
    expect(firstStep.endPc).toBe(handlerAddr);
    expect(firstStep.ppc).toBe(0x5DC20);
    const spAfterFirst = regsAfterFirst.sp >>> 0;
    expect(spAfterFirst).toBeLessThan(initialSp);
    expect(initialSp - spAfterFirst).toBeGreaterThanOrEqual(6);
    expect(regsAfterFirst.pc >>> 0).toBe(handlerAddr);
    expect(trapWrite).toBeUndefined();

    memoryWrites.length = 0;

    const secondStep = system.step();
    const regsAfterSecond = system.getRegisters();

    trapWrite = memoryWrites.find(
      (w) => w.addr === 0x00100B1C && w.size === 4 && w.value === 0xFFFFFFFF
    );

    expect(secondStep.startPc).toBe(handlerAddr);
    expect(secondStep.endPc).toBe(handlerAddr + expectedHandlerSize);
    expect(secondStep.ppc).toBe(handlerAddr);
    expect(regsAfterSecond.pc >>> 0).toBe(handlerAddr + expectedHandlerSize);
    expect(trapWrite).toBeDefined();
  });
});
