Title: Stabilize Node WASM single-step boundary + memory-trace callback ABI

Summary
- Fixes intermittent Node breakage reported by downstream integrators:
  - step() endPc drift after one-instruction step due to prefetch bookkeeping
  - "null function or function signature mismatch" when the core calls the JS memory-trace callback

Changes
- Core: step() boundary normalization
  - In myfunc.cc:m68k_step_one(), capture start PC, execute one instruction, decode size via m68k_disassemble, and write back PC = startPc + size. Also set PPC = startPc for consistent metadata.
- Core: preserve memory-trace ABI with i64 cycles
  - In m68ktrace.cc, switch from std::function to raw C function pointers for all trace callbacks to ensure the wasm function table preserves the exact signature, including the i64 cycles parameter (BigInt in JS) when built with -s WASM_BIGINT=1.

Build flags
- Verified build.sh already sets: -s WASM_BIGINT=1, -s ALLOW_TABLE_GROWTH=1, -s ALLOW_MEMORY_GROWTH=1, -s EXPORT_ES6=1, -s MODULARIZE=1.

Tests
- musashi-wasm-test (Node):
  - step_metadata_parity.test.js: PASS (endPc === startPc + decoded size)
  - memtrace_signature.test.js: PASS (callback installed with addFunction(fn, 'iiiiij'); no signature mismatch)
  - register_enum_parity.test.js: PASS (PPC reflects previous PC after stepping one instruction)
- packages/core TypeScript tests rely on staging artifacts; unaffected by this change.

Rationale
- Normalizing end PC in the core avoids downstream shims and ensures the public step() contract is stable across builds.
- Using raw C function pointers avoids ABI ambiguities introduced by std::function and guarantees the JS bridge receives i64 cycles under WASM_BIGINT.

Follow-ups (out of scope for this PR)
- Universal ESM loader env detection (separate test still flags issues unrelated to this fix).
- Consider making disassemble(address) return { text, size } at the API, and deprecate getInstructionSize, if we want a single helper.

Migration impact
- None expected for consumers. The Node memory-trace callback remains addFunction(fn, 'iiiiij'), and step() metadata becomes more stable.

