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
  it('matches enum for all known names', async () => {
    const mod = await loadModule();
    const map = parseEnumMap();
    const names = [
      'D0','D1','D2','D3','D4','D5','D6','D7',
      'A0','A1','A2','A3','A4','A5','A6','A7',
      'PC','SR','SP','PPC','USP','ISP','MSP',
      'SFC','DFC','VBR','CACR','CAAR','PREF_ADDR','PREF_DATA','IR','CPU_TYPE'
    ];
    for (const name of names) {
      const ptr = cString(mod, name);
      const got = mod._m68k_regnum_from_name(ptr);
      const exp = map.get(name);
      expect(got).toBe(exp);
      mod._free(ptr);
    }
  }, 20000);
});

