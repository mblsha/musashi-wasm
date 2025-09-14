Musashi WASM – Type De-duplication TODO

- [x] Remove redundant declaration duplicates (.d.mts)
- [x] Consolidate perfetto declarations into a single `npm-package/lib/index.d.ts`
- [x] Point both `.` and `./perfetto` `exports.types` to `./lib/index.d.ts`
- [x] Expose class wrappers as public API (Option A):
  - `exports["."].import` → `./lib/index.mjs`
  - `exports["./perfetto"].import` → `./lib/perfetto.mjs`
- [x] Centralize shared callback types in `@m68k/common`
- [x] Update `npm-package` to import shared types from `@m68k/common`

Future improvements (optional):

- [ ] Generate `.d.ts` from TS/JSDoc sources to prevent drift
- [ ] Document that the default entrypoints are isomorphic (Node + browser) and that `saveTrace` is Node-only
- [ ] Add a small CI type check that imports `musashi-wasm` and `musashi-wasm/perfetto` under both `moduleResolution: node` and `nodenext`
- [ ] If `.d.mts` are ever needed, auto-generate them from `index.d.ts` during build

Completed (Node WASM step + memory-trace contract):

- [x] Normalize PC and PPC after single-step to align with disassembler size boundary using `m68k_disassemble`
- [x] Replace `std::function` callback storage with raw C function pointers to preserve the exact ABI (i64 `cycles`) across the wasm↔JS boundary
- [x] Verified with musashi-wasm-test:
  - step_metadata_parity.test.js: PASS
  - memtrace_signature.test.js: PASS
  - register_enum_parity.test.js: PASS
