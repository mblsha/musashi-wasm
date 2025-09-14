import { readFileSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));

function parseEnumMap() {
  const dts = readFileSync(join(__dirname, '..', 'lib', 'index.d.ts'), 'utf8');
  const map = new Map();
  const enumBlock = dts.split('export enum M68kRegister')[1].split('}')[0];
  for (const line of enumBlock.split('\n')) {
    const m = line.match(/\s*([A-Z_0-9]+)\s*=\s*(\d+)/);
    if (m) map.set(m[1], parseInt(m[2], 10));
  }
  return map;
}

async function loadModule() {
  const { default: createModule } = await import('../../musashi-node.out.mjs');
  return await createModule();
}

function cString(mod, js) {
  const bytes = new TextEncoder().encode(js + '\0');
  const ptr = mod._malloc(bytes.length);
  mod.HEAPU8.set(bytes, ptr);
  return ptr;
}

describe('Register enum and native name resolution', () => {
  test('PPC enum is 19 in the d.ts', () => {
    const map = parseEnumMap();
    expect(map.get('PPC')).toBe(19);
  });

  test('m68k_regnum_from_name() matches enum for all names', async () => {
    const mod = await loadModule();
    const map = parseEnumMap();
    for (const [name, exp] of map.entries()) {
      const ptr = cString(mod, name);
      const got = mod._m68k_regnum_from_name(ptr);
      expect(got).toBe(exp);
      mod._free(ptr);
    }
  }, 20000);

  test('getReg(PPC) returns previous PC after stepping one instruction', async () => {
    const mod = await loadModule();
    const map = parseEnumMap();
    const PC = map.get('PC');
    const PPC = map.get('PPC');

    const memSize = 1 << 20; // 1MB power-of-two buffer
    const mem = new Uint8Array(memSize);
    const resetPC = 0x00000400;
    // Place a single NOP at resetPC
    mem[resetPC] = 0x4e; mem[resetPC + 1] = 0x71;

    // Wire 8-bit callbacks directly
    const read8 = mod.addFunction((addr) => mem[addr & (memSize - 1)], 'ii');
    const write8 = mod.addFunction((addr, val) => { mem[addr & (memSize - 1)] = val & 0xff; }, 'vii');
    mod._set_read8_callback(read8);
    mod._set_write8_callback(write8);

    mod._m68k_init();
    // Set SR and PC without relying on ROM vectors
    mod._set_entry_point(resetPC);

    // Step exactly one instruction
    mod._m68k_step_one();

    const pc = mod._m68k_get_reg(0, PC) >>> 0;
    const ppc = mod._m68k_get_reg(0, PPC) >>> 0;
    expect(ppc).toBe(resetPC >>> 0);
    expect(pc).toBe((resetPC + 2) >>> 0);
  }, 20000);
});

