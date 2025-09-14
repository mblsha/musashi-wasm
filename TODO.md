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
