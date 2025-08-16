# Repository Guidelines

## Project Structure & Module Organization
- `m68k*.c/h`, `myfunc.cc`, `m68ktrace.cc`: Core emulator and WASM bridge (C/C++).
- `packages/core`, `packages/memory`: TypeScript API packages published via workspaces.
- `npm-package/`: Standalone npm package wrapper and generated artifacts.
- `tests/`: CMake/GoogleTest C++ tests; `.cpp` plus helper headers.
- `musashi-wasm-test/`: JS/Node integration tests for WASM builds.
- `third_party/`: External deps (e.g., Perfetto, protobuf, vasm).

## Build, Test, and Development Commands
- Install deps: `npm install`
- Build TS workspaces: `npm run build`
- Test TS workspaces: `npm test` (e.g., `npm test --workspace=@m68k/core`)
- Type check: `npm run typecheck`
- Build WASM (standard): `./build.fish` or `./build_wasm_simple.sh`
- Build WASM with Perfetto: `ENABLE_PERFETTO=1 ./build.fish`
- Native CMake tests: `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`
- Package wrapper build: `cd npm-package && npm run build`

## Coding Style & Naming Conventions
- TypeScript: ES2020, strict types, modules via workspaces. Use `camelCase` for functions/variables, `PascalCase` for types. Tests end with `.test.ts` and live near `src`.
- C/C++: C++17/C99; warnings enabled. Match existing file patterns (`snake_case` filenames, small, focused units). Prefer clear ownership over macros.
- Formatting/Linting: No repo-wide linter configured; keep diffs minimal and consistent. Use `tsc --noEmit` to catch type issues before PRs.

## Testing Guidelines
- TypeScript: Jest with ts-jest. Place tests as `**/*.test.ts`. Example: `npm test --workspace=@m68k/memory`.
- C++: GoogleTest via CMake/CTest. Build and run with the native CMake commands above. Perfetto-specific tests require `ENABLE_PERFETTO=ON` and protobuf setup.
- Aim for tests that cover register access, memory hooks, and basic execution paths. Include failure cases (invalid addresses, tracing off/on).

## Commit & Pull Request Guidelines
- Commits: Imperative mood, concise scope (e.g., "Fix M68000 exception handling", "Add TS wrapper for tracing"). Squash noise; group related changes.
- PRs: Include a clear description, linked issues, and test evidence (commands run, output, or screenshots when relevant). Ensure `npm test`, `npm run typecheck`, and CMake tests pass. Note changes affecting `npm-package/` or WASM artifacts.

## Security & Configuration Tips
- Tooling: Emscripten SDK required for WASM; Node 16+; CMake for native. Set `ENABLE_PERFETTO=1` only when protobuf/abseil are built (see `build_protobuf_wasm.sh`). Avoid committing generated binaries except where expected (`npm-package/dist`, `packages/core/wasm`).

