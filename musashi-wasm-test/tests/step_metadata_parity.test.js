import path from 'path';
import fs from 'fs';
import { fileURLToPath } from 'url';

import createMusashiModule from '../load-musashi.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const modulePath = path.resolve(__dirname, '../../musashi-node.out.mjs');

if (!fs.existsSync(modulePath)) {
  console.error(`\n============================================`);
  console.error(`ERROR: WASM module not found at ${modulePath}`);
  console.error(`============================================\n`);
  console.error(`Run ./build.sh from the repo root to build the module.`);
  console.error(`\n============================================\n`);
  process.exit(1);
}

describe('Single-step metadata parity against disassembler size', () => {
  let Module;

  beforeAll(async () => {
    Module = await createMusashiModule();
  });

  it('advances PC by decoded size for a simple sequence', () => {
    const REGION_SIZE = 64 * 1024;
    const memPtr = Module._malloc(REGION_SIZE);
    expect(memPtr).not.toBe(0);
    const mem = Module.HEAPU8.subarray(memPtr, memPtr + REGION_SIZE);

    // Map region
    Module.ccall('add_region', 'void', ['number', 'number', 'number'], [0, REGION_SIZE, memPtr]);

    // Reset vectors: SP=0x10000, PC=0x400
    mem[0] = 0x00; mem[1] = 0x01; mem[2] = 0x00; mem[3] = 0x00;
    mem[4] = 0x00; mem[5] = 0x00; mem[6] = 0x04; mem[7] = 0x00;

    // Program at 0x400:
    // 0x400: MOVE.L #$12345678, D0 (6 bytes)
    // 0x406: MOVE.L D0, (A0)       (2 bytes)
    // 0x408: ADD.L #1, D1          (6 bytes)
    const program = new Uint8Array([
      0x20, 0x3c, 0x12, 0x34, 0x56, 0x78,
      0x20, 0x80,
      0x06, 0x81, 0x00, 0x00, 0x00, 0x01,
    ]);
    mem.set(program, 0x400);

    try {
      // Initialize and reset CPU
      Module._m68k_init();
      Module._m68k_pulse_reset();

      const stepAndAssert = (pc) => {
        // Decode size via disassembler
        const buf = Module._malloc(128);
        const size = Module.ccall('m68k_disassemble', 'number', ['number','number','number'], [buf, pc, 0]);
        Module._free(buf);
        expect(size).toBeGreaterThan(0);

        const start = Module._m68k_get_reg(0, 16) >>> 0;
        expect(start >>> 0).toBe(pc >>> 0);
        const cyc = Module._m68k_step_one();
        expect(cyc).toBeGreaterThan(0);
        const end = Module._m68k_get_reg(0, 16) >>> 0;
        expect(end >>> 0).toBe((start + size) >>> 0);
        return end >>> 0;
      };

      // A0 used by MOVE.L D0,(A0) second instruction
      Module._m68k_set_reg(8, 0x100000);

      let pc = 0x400;
      pc = stepAndAssert(pc);
      pc = stepAndAssert(pc);
      pc = stepAndAssert(pc);
    } finally {
      try { Module._free(memPtr); } catch {}
      try { Module.ccall('clear_regions', 'void', [], []); } catch {}
    }
  });
});

