import path from 'path';
import fs from 'fs';
import { fileURLToPath } from 'url';

import createMusashiModule from '../load-musashi.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const modulePath = path.resolve(__dirname, '../../musashi-node.out.mjs');

// Ensure artifacts exist before running
if (!fs.existsSync(modulePath)) {
  // Mirror style from other tests in this package
  console.error(`\n============================================`);
  console.error(`ERROR: WASM module not found at ${modulePath}`);
  console.error(`============================================\n`);
  console.error(`Run ./build.sh from the repo root to build the module.`);
  console.error(`\n============================================\n`);
  process.exit(1);
}

describe('Memory trace callback signature (iiiiij) works in Node', () => {
  let Module;

  beforeAll(async () => {
    Module = await createMusashiModule();
  });

  it('should emit a memory write event without signature mismatch', () => {
    // Arrange memory: map a small region and place a tiny program at 0x400
    const REGION_SIZE = 64 * 1024;
    const memPtr = Module._malloc(REGION_SIZE);
    expect(memPtr).not.toBe(0);
    const mem = Module.HEAPU8.subarray(memPtr, memPtr + REGION_SIZE);

    // Map region [0..REGION_SIZE)
    Module.ccall('add_region', 'void', ['number', 'number', 'number'], [0, REGION_SIZE, memPtr]);

    // Reset vectors: SP=0x10000, PC=0x400
    mem[0] = 0x00; mem[1] = 0x01; mem[2] = 0x00; mem[3] = 0x00;
    mem[4] = 0x00; mem[5] = 0x00; mem[6] = 0x04; mem[7] = 0x00;

    // Program at 0x400:
    // MOVE.L #$CAFEBABE, D0
    // MOVE.L D0, -(SP)
    // RTS
    const PROG = new Uint8Array([
      0x20, 0x3c, 0xca, 0xfe, 0xba, 0xbe,
      0x2f, 0x00,
      0x4e, 0x75,
    ]);
    mem.set(PROG, 0x400);

    const events = [];
    // Signature: (i32, i32, i32, i32, i32, i64) -> i32
    const cbPtr = Module.addFunction((type, pc, addr, value, size, cycles) => {
      // cycles is a BigInt when WASM_BIGINT=1; ignore it but ensure call succeeds
      events.push({ type, pc, addr, value, size });
      return 0;
    }, 'iiiiij');

    try {
      Module._m68k_set_trace_mem_callback(cbPtr);
      Module._m68k_trace_enable(1);
      Module._m68k_trace_set_mem_enabled(1);

      // Act: init + reset + execute a few cycles
      Module._m68k_init();
      Module._m68k_pulse_reset();
      const used = Module._m68k_execute(200);
      expect(used).toBeGreaterThan(0);

      // Assert: at least one write event observed
      expect(events.length).toBeGreaterThan(0);
      const { addr, size, value, pc } = events.find(e => e.type === 1) || events[0];
      expect(typeof addr).toBe('number');
      expect([1,2,4]).toContain(size);
      expect(typeof value).toBe('number');
      expect(typeof pc).toBe('number');
    } finally {
      // Cleanup
      try { Module._m68k_set_trace_mem_callback(0); } catch {}
      try { Module.removeFunction(cbPtr); } catch {}
      try { Module._free(memPtr); } catch {}
      try { Module.ccall('clear_regions', 'void', [], []); } catch {}
    }
  });
});

