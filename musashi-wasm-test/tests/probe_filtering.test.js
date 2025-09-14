import path from 'path';
import fs from 'fs';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Ensure WASM module exists
const modulePath = path.resolve(__dirname, '../../musashi-node.out.mjs');
if (!fs.existsSync(modulePath)) {
  console.error(`Missing WASM module at ${modulePath}. Build via ./build.sh`);
  process.exit(1);
}

import createMusashiModule from '../load-musashi.js';

describe('JS probe respects address filtering', () => {
  let Module;
  const PROG_START = 0x400;

  beforeAll(async () => {
    Module = await createMusashiModule();
    expect(Module).toBeDefined();
  });

  beforeEach(() => {
    Module._m68k_init();
    Module._clear_regions();
  });

  it('calls probe only for filtered PCs', () => {
    const MEM_SIZE = 0x2000;
    const memPtr = Module._malloc(MEM_SIZE);
    const mem = Module.HEAPU8.subarray(memPtr, memPtr + MEM_SIZE);

    try {
      // Map region
      Module._add_region(0, MEM_SIZE, memPtr);

      // Reset vectors
      mem[0] = 0x00; mem[1] = 0x00; mem[2] = 0x10; mem[3] = 0x00; // SP
      mem[4] = 0x00; mem[5] = 0x00; mem[6] = 0x04; mem[7] = 0x00; // PC -> 0x400

      // Program: 6 NOPs starting at 0x400 (PC values: 0x400,0x402,...,0x40A)
      for (let i = 0; i < 6; i++) {
        mem[PROG_START + 2 * i] = 0x4E; // NOP
        mem[PROG_START + 2 * i + 1] = 0x71;
      }

      // Set JS probe
      let calls = [];
      const probeFunc = Module.addFunction((pc) => { calls.push(pc); return 0; }, 'ii');
      Module._set_pc_hook_func(probeFunc);

      // Filter to only one PC: 0x404
      Module._add_pc_hook_addr(0x404);

      // Reset and run
      Module._m68k_pulse_reset();
      Module._m68k_execute(100);

      // Expect probe called only for filtered address
      const unique = [...new Set(calls)];
      expect(unique).toEqual([0x404]);

      Module.removeFunction(probeFunc);
    } finally {
      Module._free(memPtr);
      Module._clear_regions();
    }
  });
});
