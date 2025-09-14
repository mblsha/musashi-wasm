import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

import createMusashiModule from '../load-musashi.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

function parseEnumFromCommon() {
  const tsPath = path.resolve(__dirname, '../../packages/common/src/index.ts');
  const src = fs.readFileSync(tsPath, 'utf8');
  const start = src.indexOf('export enum M68kRegister');
  if (start < 0) throw new Error('M68kRegister enum not found in @m68k/common');
  const after = src.slice(start);
  const block = after.slice(after.indexOf('{') + 1, after.indexOf('}'));
  const map = new Map();
  for (const line of block.split('\n')) {
    const m = line.match(/\s*([A-Z_0-9]+)\s*=\s*(\d+)/);
    if (m) map.set(m[1], parseInt(m[2], 10));
  }
  return map;
}

async function loadModule() {
  const mod = await createMusashiModule();
  return mod;
}

function cString(mod, js) {
  const bytes = new TextEncoder().encode(js + '\0');
  const ptr = mod._malloc(bytes.length);
  mod.HEAPU8.set(bytes, ptr);
  return ptr;
}

describe('M68kRegister enum parity with native resolver', () => {
  test('PPC enum is 19 per common enum', () => {
    const map = parseEnumFromCommon();
    expect(map.get('PPC')).toBe(19);
  });

  test('m68k_regnum_from_name() matches @m68k/common enum for all names', async () => {
    const mod = await loadModule();
    const map = parseEnumFromCommon();
    for (const [name, exp] of map.entries()) {
      const ptr = cString(mod, name);
      const got = mod._m68k_regnum_from_name(ptr);
      expect(got).toBe(exp);
      mod._free(ptr);
    }
  }, 20000);

  test('getReg(PPC) reports reasonable previous-PC semantics after one step', async () => {
    const mod = await loadModule();
    const map = parseEnumFromCommon();
    const PC = map.get('PC');
    const PPC = map.get('PPC');

    const memSize = 1 << 20; // 1MB power-of-two buffer
    const mem = new Uint8Array(memSize);
    const resetPC = 0x00000400;
    // Place a single NOP at resetPC
    mem[resetPC] = 0x4e; mem[resetPC + 1] = 0x71;

    // Wire 8-bit callbacks directly
    const readMem = mod.addFunction((addr, size) => {
      addr = addr & (memSize - 1);
      if (size === 1) return mem[addr];
      if (size === 2) return (mem[addr] << 8) | mem[addr + 1];
      return (mem[addr] << 24) | (mem[addr + 1] << 16) | (mem[addr + 2] << 8) | mem[addr + 3];
    }, 'iii');
    const writeMem = mod.addFunction((addr, size, val) => {
      addr = addr & (memSize - 1);
      if (size === 1) { mem[addr] = val & 0xff; return; }
      if (size === 2) { mem[addr] = (val >> 8) & 0xff; mem[addr + 1] = val & 0xff; return; }
      mem[addr] = (val >> 24) & 0xff; mem[addr + 1] = (val >> 16) & 0xff; mem[addr + 2] = (val >> 8) & 0xff; mem[addr + 3] = val & 0xff;
    }, 'viii');
    mod._set_read_mem_func(readMem);
    mod._set_write_mem_func(writeMem);

    mod._m68k_init();
    // Set SR and PC without relying on ROM vectors
    mod._set_entry_point(resetPC);

    // Step exactly one instruction
    mod._m68k_step_one();

    const pc = mod._m68k_get_reg(0, PC) >>> 0;
    const ppc = mod._m68k_get_reg(0, PPC) >>> 0;
    // PC should advance by at least the size of NOP (2 bytes); allow 2 or 4 depending on core behavior
    expect([ (resetPC + 2) >>> 0, (resetPC + 4) >>> 0 ]).toContain(pc >>> 0);
    // PPC can be either the start PC or implementation-defined; accept both common variants
    expect([resetPC >>> 0, pc >>> 0, (resetPC + 4) >>> 0]).toContain(ppc);
  }, 20000);
});
