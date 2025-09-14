const fs = require('fs');
const path = require('path');
const Musashi = require('../lib/index.js');

function enumValueFromDts(name) {
  const dtsPath = path.join(__dirname, '..', 'lib', 'index.d.ts');
  const text = fs.readFileSync(dtsPath, 'utf8');
  const re = new RegExp(`\\b${name}\\s*=\\s*(\\d+)`);
  const m = text.match(re);
  if (!m) throw new Error(`Enum ${name} not found in index.d.ts`);
  return parseInt(m[1], 10);
}

describe('M68kRegister.PPC enum value', () => {
  it('is 19 in the d.ts', () => {
    const ppc = enumValueFromDts('PPC');
    expect(ppc).toBe(19);
  });

  it('works with getReg() to read previous PC', async () => {
    const memSize = 1024 * 1024; // 1MB (power of two)
    const resetSP = 0x00001000;
    const resetPC = 0x00000400;
    const PC = enumValueFromDts('PC');
    const PPC = enumValueFromDts('PPC');

    const cpu = new Musashi();
    await cpu.init();
    const memPtr = cpu.allocateMemory(memSize);

    const memory = new Uint8Array(memSize);
    // Reset vectors (big-endian)
    memory[0] = (resetSP >> 24) & 0xff;
    memory[1] = (resetSP >> 16) & 0xff;
    memory[2] = (resetSP >> 8) & 0xff;
    memory[3] = resetSP & 0xff;
    memory[4] = (resetPC >> 24) & 0xff;
    memory[5] = (resetPC >> 16) & 0xff;
    memory[6] = (resetPC >> 8) & 0xff;
    memory[7] = resetPC & 0xff;
    // First instruction: NOP at resetPC
    memory[resetPC] = 0x4e;
    memory[resetPC + 1] = 0x71;

    cpu.setReadMemFunc(address => memory[address & (memSize - 1)]);
    cpu.setWriteMemFunc((address, value) => {
      memory[address & (memSize - 1)] = value & 0xff;
    });

    cpu.writeMemory(memPtr, memory);
    cpu.addRegion(0, memSize, memPtr);

    let firstPc = -1;
    cpu.setPCHookFunc(pc => {
      if (firstPc === -1) {
        firstPc = pc >>> 0;
        return 1; // stop after first instruction boundary is seen
      }
      return 0;
    });

    cpu.pulseReset();
    // Sanity: PC is the reset vector
    expect(cpu.getReg(PC) >>> 0).toBe(resetPC >>> 0);

    // Run a small slice; PC hook will immediately stop at first instruction
    cpu.execute(50);

    // The hook saw the instruction start PC
    expect(firstPc >>> 0).toBe(resetPC >>> 0);
    // PPC should reflect the previous PC, which equals the firstPc we observed
    const ppcVal = cpu.getReg(PPC) >>> 0;
    expect(ppcVal).toBe(firstPc >>> 0);

    cpu.clearRegions();
    cpu.freeMemory(memPtr);
  });
});

