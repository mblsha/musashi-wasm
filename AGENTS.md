# Repository Guidelines

## Project Structure & Module Organization
- Core emulator and WASM bridge (C/C++): `m68k*.c/h`, `myfunc.cc`, `m68ktrace.cc`.
- TypeScript packages: `packages/core`, `packages/memory` (published via workspaces).
- NPM wrapper & artifacts: `npm-package/`.
- C++ tests: `tests/` (CMake/GoogleTest).
- WASM integration tests: `musashi-wasm-test/`.
- External deps: `third_party/` (Perfetto, protobuf, vasm).

## Architecture Overview
- Musashi 680x0 core powers CPU emulation; C sources target native and WASM via Emscripten.
- A thin C/WASM bridge exposes the emulator API; `@m68k/core` wraps it for TypeScript, with memory helpers in `@m68k/memory`.
- Optional tracing hooks emit Perfetto events when built with `ENABLE_PERFETTO=1`.
- `npm-package/` distributes prebuilt WASM/JS bindings for Node and browsers.

## Build, Test, and Development Commands
- Install deps: `npm install`.
- Build TS workspaces: `npm run build`.
- Test TS: `npm test` (e.g., `npm test --workspace=@m68k/core`).
- Type check: `npm run typecheck` (runs `tsc --noEmit`).
- Build WASM: `./build.fish` or `./build_wasm_simple.sh`.
- Build WASM with Perfetto: `ENABLE_PERFETTO=1 ./build.fish`.
- Native C++ tests: `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`.
- Package wrapper: `cd npm-package && npm run build`.

## Coding Style & Naming Conventions
- TypeScript: ES2020 modules, strict types. `camelCase` for functions/vars, `PascalCase` for types. Tests end with `.test.ts` near `src`.
- C/C++: C++17/C99. Filenames `snake_case`. Prefer small, focused units; clear ownership over macros. Warnings enabled; fix new warnings.
- Formatting: No repo-wide linter; keep diffs minimal and consistent.

## Testing Guidelines
- TypeScript: Jest via ts-jest. Place tests as `**/*.test.ts`. Aim to cover register access, memory hooks, basic execution; include failure cases.
- C++: GoogleTest via CMake/CTest. Perfetto-specific tests require `ENABLE_PERFETTO=ON` and protobuf/abseil (see `build_protobuf_wasm.sh`).

## Commit & Pull Request Guidelines
- Commits: Imperative, scoped (e.g., “Fix M68000 exception handling”, “Add TS wrapper for tracing”). Squash noise; group related changes.
- PRs: Include clear description, linked issues, and test evidence (commands run, output/screenshots). Ensure `npm test`, `npm run typecheck`, and CMake tests pass. Note changes affecting `npm-package/` or `packages/core/wasm`.

## Security & Configuration Tips
- Requirements: Emscripten SDK (for WASM), Node 16+, CMake.
- Enable Perfetto only when protobuf/abseil are built.
- Avoid committing generated binaries except `npm-package/dist` and `packages/core/wasm`.
