/**
 * WASM setjmp smoke test
 *
 * Purpose
 * - Exercise a minimal CPU init+step via the Node ESM build.
 * - With emsdk 3.1.74, this should run without aborts (JS setjmp glue available).
 * - With emsdk 4.x (default flags), this typically aborts at runtime with
 *   "missing function: saveSetjmp" due to setjmp/longjmp runtime changes.
 *
 * Usage
 *   node tests/test_wasm_setjmp_smoke.mjs
 *   # exit code 0 on success, nonzero on failure
 */

import createMusashi from '../musashi-node.out.mjs';

function noopRead(address, size) {
  // Return 0 for any address/size to keep the core running without traps.
  return 0 >>> 0;
}
function noopWrite(address, size, value) {
  // No-op write
}

async function main() {
  try {
    const mod = await createMusashi({
      print: (...args) => console.log('[emscripten]', ...args),
      printErr: (...args) => console.error('[emscripten-err]', ...args)
    });

    // Register minimal read/write handlers
    const readPtr = mod.addFunction((addr, size) => noopRead(addr >>> 0, size >>> 0), 'iii');
    const writePtr = mod.addFunction((addr, size, value) => noopWrite(addr >>> 0, size >>> 0, value >>> 0), 'viii');
    mod._set_read_mem_func(readPtr);
    mod._set_write_mem_func(writePtr);

    // CPU init + single step
    mod._m68k_init();
    mod._m68k_set_context(0);
    mod._m68k_pulse_reset();
    mod._m68k_execute(1);

    console.log('[setjmp-smoke] ok');
    process.exit(0);
  } catch (e) {
    console.error('[setjmp-smoke] failed:', e && (e.stack || e.message || e));
    process.exit(1);
  }
}

await main();

