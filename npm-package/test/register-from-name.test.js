import { readFileSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));

function parseEnumMap() {
  const dts = readFileSync(join(__dirname, '..', 'lib', 'index.d.ts'), 'utf8');
  const map = new Map();
  const enumBlock = dts.split('export enum M68kRegister')[1].split('}')[0];
  const enumLines = enumBlock.split('\n');
  for (const line of enumLines) {
    const m = line.match(/\s*([A-Z_0-9]+)\s*=\s*(\d+)/);
    if (m) map.set(m[1], parseInt(m[2], 10));
  }
  return map;
}

async function loadModule() {
  // Load local WASM ESM build (node variant) from repo root
  const { default: createModule } = await import('../../musashi-node.out.mjs');
  return await createModule();
}

function cString(mod, js) {
  const bytes = new TextEncoder().encode(js + '\0');
  const ptr = mod._malloc(bytes.length);
  mod.HEAPU8.set(bytes, ptr);
  return ptr;
}

describe('m68k_regnum_from_name()', () => {
  it('matches enum for all register names found in d.ts', async () => {
    const mod = await loadModule();
    const map = parseEnumMap();
    for (const [name, exp] of map.entries()) {
      const ptr = cString(mod, name);
      const got = mod._m68k_regnum_from_name(ptr);
      expect(got).toBe(exp);
      mod._free(ptr);
    }
  }, 20000);
});
