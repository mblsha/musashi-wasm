# Memory Trace Callback Repro (Node, WASM_BIGINT=1)

This document captures a minimal reproduction of a memory‑trace callback crash observed in a downstream environment, plus exact build/run steps to keep the repro reproducible.

Note: Upstream CI reports this path as working and has tests that validate the signature (iiiiij). If this repro does not crash on your machine, it points to an environment/tooling mismatch rather than a defect here.

## Environment

- Commit: `git rev-parse HEAD` (example during repro: `3c3e28c6cfc9` on master)
- Toolchain: Emscripten 4.0.12 (repo‑bundled `emsdk/`)
- Node.js: v20.18.1 (Linux aarch64)
- Build flags (from `build.sh`):
  - `-s WASM_BIGINT=1`
  - `-s ALLOW_TABLE_GROWTH=1`
  - `-s ALLOW_MEMORY_GROWTH=1`
  - Node ESM bundle at repo root: `musashi-node.out.mjs/.wasm`
  - Staged Node wrapper in `packages/core/wasm/` loads `musashi-node.out.mjs`

## Build (exact commands)

From the repo root:

```bash
EMSDK=$(pwd)/emsdk ENABLE_PERFETTO=0 ./build.sh
cp -v musashi-node.out.mjs musashi-node.out.wasm packages/core/wasm/
shasum -a 256 musashi-node.out.{mjs,wasm} packages/core/wasm/musashi-node.out.{mjs,wasm}
```

## Repro script

Run the provided script:

```bash
node scripts/memtrace-repro.mjs
```

Expected (healthy): at least one WRITE and one READ event logged.

Observed (downstream crash):

```
RuntimeError: null function or function signature mismatch
  at m68k_trace_mem_hook (wasm)
  at m68k_op_move_32_* (wasm)
  at invoke_v ... getWasmTableEntry(index)()
```

## Cross‑checks

- Confirm the callback is installed with `'iiiiij'` (WASM_BIGINT=1) in `packages/core/dist/musashi-wrapper.js`.
- Optionally run upstream memtrace validation test:
  - `node musashi-wasm-test/tests/memtrace_signature.test.js`

