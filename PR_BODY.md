Title: Core TS API ergonomics: unified hooks, disassemblyDetailed, dispose + migration guide; fix npm M68kRegister enum

Summary
- Add a more concise and flexible TS API surface in @m68k/core:
  - Unified PC hooks via `system.addHook(address, handler)` returning `'continue' | 'stop'`.
  - Rich disassembly via `system.disassembleDetailed(pc) -> { text, size }`.
  - Lifecycle `system.dispose()` to release WASM callbacks/regions.
  - Re-export `MemoryAccessEvent` and `MemoryAccessCallback` from the public entry.
- Correct the `M68kRegister` enum in the npm wrapper to match Musashi headers (notably, `PPC = 19`).
- Document changes in a new Migration Guide.

Motivation
- Reduce redundancy in the public API and make common flows easier to express while keeping compatibility.
- Fix a long-standing mismatch in the npm wrapper enum that could confuse consumers relying on raw values.

Changes
- @m68k/core
  - feat: `addHook(address, handler)`, `disassembleDetailed(pc)`, `dispose()`.
  - docs: Migration Guide with mappings and deprecation timeline.
  - types: export `MemoryAccessEvent`, `MemoryAccessCallback` from index.
- musashi-wasm (npm-package)
  - fix: align `M68kRegister` enum (PPC=19; shifted subsequent values per Musashi `m68k.h`).

Compatibility
- Additive only; existing `probe`, `override`, `disassemble`, `getInstructionSize` remain.
- Future minor: mark `probe`/`override` deprecated in types.
- Future major: prefer `@m68k/core` as the primary TS API; reduce duplication in wrapper.

Testing
- Typecheck: `npm run typecheck` passes.
- Core Jest tests require real WASM artifacts. Use `./test_with_real_wasm.sh` (or `SKIP_WASM_BUILD=1` if artifacts are present) to run locally; existing tests continue to pass once artifacts are in place.

Docs
- Added Migration Guide to `packages/README.md`.

Notes
- No runtime behavior changes unless new APIs are adopted.

