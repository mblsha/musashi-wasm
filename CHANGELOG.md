# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.12] - 2025-09-23

### ✨ Features

- **Core fusion wrapper**: Ship the compiled `@m68k/core` runtime and wasm shim as part of the `musashi-wasm` npm package so downstream tooling can call `import { createSystem } from 'musashi-wasm/core'` without vendoring sources.

### 🛠️ Tooling

- **Node typings & publish flow**: Bundle a `musashi-wasm/node` declaration file, extend package exports/tests to guard the new surface, and update the publish workflow to stage the new assets before uploading to npm.

## [0.1.11] - 2025-09-23

> ⚠️ Release superseded. Publishing to npm failed for this tag; use 0.1.12 instead.

### ✨ Features

- **Core fusion wrapper**: Ship the compiled `@m68k/core` runtime and wasm shim as part of the `musashi-wasm` npm package so downstream tooling can call `import { createSystem } from 'musashi-wasm/core'` without vendoring sources.

### 🛠️ Tooling

- **Node typings**: Bundle a `musashi-wasm/node` declaration file and extend package exports/tests to guard the new surface during future releases.

## [0.1.10] - 2025-09-22

### 🐛 Bug Fixes

- **npm package completeness**: Copy `musashi-node.out.{mjs,wasm}` into the published tarball and expose a `node` export so backend provisioning works without vendored artifacts.

### 🛠️ Tooling

- **Release automation**: Update the npm publish workflow to stage the Node-specific build from CI artifacts and include it in the package manifest.

## [0.1.9] - 2025-09-20

> ⚠️ This release was published without the Musashi Node artifacts due to a packaging regression. Please use 0.1.10 instead.

### 🐛 Bug Fixes

- **npm package completeness**: Publish the Musashi Node loader (`musashi-node.out.mjs`) and WASM binaries alongside the browser build so Node integrations work without vendoring artifacts.

### 🛠️ Tooling

- **Packaging guardrails**: Fail npm packaging if the Musashi Node build is missing and mirror the `.wasm.map` into both `dist/` and the package root to keep artifacts in sync.

## [0.1.8] - 2025-09-19

### 🚀 Improvements

- **Synchronous APIs**: `System.step`, `System.call`, `System.run`, and `tracer.stop` are now synchronous, simplifying integrations that were previously `await`-heavy.
- **Runtime robustness**: Refactored memory layout detection and Perfetto string helpers for clearer error handling and smaller WASM glue.

### 🐛 Bug Fixes

- **Call-session stability**: Install and restore sentinel stack frames so `system.call()` returns promptly even when memory tracing or JS hooks are active.
- **Memory accuracy**: Fix unsigned 32-bit reads and tighten memory bounds checks when forwarding events from the WASM core.
- **Documentation/tests**: Added detailed analysis of the F-line trap regression and updated tests to reflect the new execution semantics.

## [0.1.7] - 2025-09-16

### 🐛 Bug Fixes

- **Node import compatibility**: Ensure `@m68k/common` emits ES modules so `@m68k/core`'s dist bundle loads under native Node ESM environments without missing export errors.

## [0.1.3] - 2025-08-17

### 🐛 Bug Fixes

- **Browser compatibility fix**: Fixed critical browser compatibility issue where `fileURLToPath` was imported at the top level, breaking Vite/webpack builds
  - The fix uses conditional dynamic imports - Node.js modules are only loaded in Node.js environments
  - Browser environments now use `URL.href` directly without importing Node.js-specific modules
  - This resolves build errors in modern bundlers like Vite and webpack

### 📚 Documentation

- **Hook function clarification**: Clarified hook function usage in documentation
  - `_set_pc_hook_func` takes only PC parameter (signature 'ii'): `int hook(unsigned int pc)`
  - `_set_full_instr_hook_func` provides PC, instruction register, and cycles (signature 'iiii'): `int hook(unsigned int pc, unsigned int ir, unsigned int cycles)`
  - Added clear guidance on when to use each hook type for different use cases

## [0.1.1] - 2025-08-16

### 🚨 Breaking Changes

- **ESM-only migration**: Migrated entire repository to ESM-only architecture ([331e0b4](https://github.com/micahsnyder/musashi/commit/331e0b4))
  - Switch TypeScript packages to ESM (module ES2020, bundler resolution)
  - Set package.json `type: "module"` and ESM-only exports
  - Update core imports to `.js` and add Jest ESM config
  - Remove CommonJS resolver; map local `.js`→`.ts` for tests
  - Build WASM as ESM (EXPORT_ES6), emit `.mjs` loader
  - npm-package: ESM-only wrappers, export `lib/*.mjs`, copy ESM loader/wasm
  - Convert musashi-wasm-test to ESM; skip Perfetto when unavailable

### ✨ Features

- **Standardized build tooling** ([413b688](https://github.com/micahsnyder/musashi/commit/413b688))
  - Add npm script wrappers for shell scripts (`build:wasm`, `build:wasm:perfetto`, `build:protobuf`)
  - Add testing workflow scripts (`test:local-ci`, `test:with-wasm`)
  - Standardize Jest execution patterns across all packages
  - Unified NODE_OPTIONS configuration for ESM modules

- **Code quality tools** ([413b688](https://github.com/micahsnyder/musashi/commit/413b688))
  - Add ESLint and Prettier for TypeScript linting
  - Configure reasonable TypeScript linting rules
  - Add format/lint scripts at root and package level
  - Create ignore files for generated/third-party code
  - Auto-format existing TypeScript files

### 🐛 Bug Fixes

- **TypeScript support improvements**
  - Add missing `.d.mts` declaration files for ESM `.mjs` imports ([a2fbdbb](https://github.com/micahsnyder/musashi/commit/a2fbdbb))
  - Add `musashi-node-wrapper.d.mts` for TypeScript resolution
  - Add `index.d.mts` and `perfetto.d.mts` for npm-package ESM support
  - Suppress TypeScript errors for `.mjs` imports ([eac9a57](https://github.com/micahsnyder/musashi/commit/eac9a57))

- **CI and workflow fixes**
  - Fix Jest invocation for ESM workspaces ([0bfb8f5](https://github.com/micahsnyder/musashi/commit/0bfb8f5))
  - Prevent overwriting `musashi-node-wrapper.mjs` in CI ([bf569a3](https://github.com/micahsnyder/musashi/commit/bf569a3))
  - Update workflows and tests for ESM-only artifacts ([fdb3fc4](https://github.com/micahsnyder/musashi/commit/fdb3fc4))
  - Fix ESM mocking for TypeScript CI ([a71ddb5](https://github.com/micahsnyder/musashi/commit/a71ddb5))

- **Testing improvements**
  - Implement proper ESM mocking with `jest.unstable_mockModule` ([cb19c10](https://github.com/micahsnyder/musashi/commit/cb19c10))
  - Update tests to use correct System API and improve mocking ([bf569a3](https://github.com/micahsnyder/musashi/commit/bf569a3))
  - Add mock for musashi-wrapper to fix unit tests ([4020ea2](https://github.com/micahsnyder/musashi/commit/4020ea2))
  - Make ESM integration tests robust and pass ([9a60fbc](https://github.com/micahsnyder/musashi/commit/9a60fbc))
    - Run Jest in ESM mode, fix `__dirname`, import jest globals
    - Use correct Emscripten legacy callback signatures ('iii'/'viii')
    - Accept decomposed 32-bit writes (2x16 or 4x8) and reconstruct value
    - Call `_m68k_get_reg` with `context=0` to match arity
    - Skip Perfetto bodies when not available

### 🔄 Refactoring

- **Test architecture overhaul** ([30a4bb9](https://github.com/micahsnyder/musashi/commit/30a4bb9))
  - Migrate from mock to real WASM testing
  - Remove mock implementation (`packages/core/src/__mocks__/musashi-wrapper.ts`)
  - Remove mock-specific tests (`packages/core/src/mock-only.test.ts`)
  - Update core tests to use real WASM with proper expectations
  - Remove mock module mapping from Jest config

### 📚 Documentation

- **File reference updates** ([afdf51f](https://github.com/micahsnyder/musashi/commit/afdf51f))
  - Update CLAUDE.md to reference `myfunc.cc` instead of `myfunc.c` throughout
  - Update comment in `test_myfunc.cpp` to reference correct filename
  - Remove outdated note about `.c` extension requiring C++ compilation
  - All build files already correctly reference `myfunc.cc`

### 🧹 Cleanup

- **Build script consolidation** ([81815b4](https://github.com/micahsnyder/musashi/commit/81815b4))
  - Remove `build_wasm_simple.sh` (use `build.fish`)
  - Remove `build_perfetto_wasm_simple.sh` (use `ENABLE_PERFETTO=1 ./build.fish`)
  - Remove `npm-package/scripts/build-local.sh` (redundant; use `build.fish` + wrapper gen)

- **Build artifacts management**
  - Switch to ESM-only artifacts (`.mjs`) ([52eb35b](https://github.com/micahsnyder/musashi/commit/52eb35b))
  - Add EXPORT_ES6=1, emit `.mjs` for node/web loaders
  - Update CI to verify/upload `.mjs` instead of `.js` in both jobs
  - Stage `.mjs` loaders; publish ESM-only entries (no CJS)
  - Update gitignore for new ESM build artifacts ([719abaf](https://github.com/micahsnyder/musashi/commit/719abaf))

### 🏗️ Build System

- **CI improvements**
  - Add diagnostics and fix Jest invocation for workspaces ([debug_ci](https://github.com/micahsnyder/musashi/commit/debug_ci))
  - Standardize Jest execution with consistent `NODE_OPTIONS='--experimental-vm-modules'`
  - Add missing `test:ci` scripts to all packages

## [0.1.0] - Previous Release

Initial release of Musashi M68000 CPU emulator with WebAssembly support.

---

## Release Highlights for v0.1.1

### 🎯 Major Achievement: Complete ESM Migration
Version 0.1.1 represents a significant architectural milestone with the complete migration to ECMAScript Modules (ESM). This modernizes the entire codebase and provides better compatibility with modern JavaScript tooling and bundlers.

### 🛠️ Developer Experience Improvements
- **Unified Build System**: Consolidated build scripts and added npm wrapper commands
- **Code Quality**: Added ESLint and Prettier for consistent code formatting
- **Real WASM Testing**: Migrated from mock-based to real WebAssembly testing for better reliability

### 🔧 Stability and Compatibility
- **TypeScript Support**: Complete `.d.mts` declaration files for ESM imports
- **CI Robustness**: Fixed multiple CI issues and improved test reliability
- **Documentation**: Updated all file references and cleaned up outdated information

This release maintains full backward compatibility for the WebAssembly API while modernizing the underlying build and packaging infrastructure.
