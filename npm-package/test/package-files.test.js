const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const pkgRoot = path.join(__dirname, '..');
const distDir = path.join(pkgRoot, 'dist');
const libDir = path.join(pkgRoot, 'lib');
const coreDir = path.join(libDir, 'core');
const coreWasmDir = path.join(libDir, 'wasm');

const MIN_WASM_SIZE = 16 * 1024; // bytes, guards against empty placeholders

function ensureBuilt() {
  if (fs.existsSync(path.join(pkgRoot, 'musashi-node.out.mjs'))) {
    return;
  }

  const result = spawnSync(process.execPath, [path.join(pkgRoot, 'scripts', 'generate-wrapper.js')], {
    cwd: pkgRoot,
    stdio: 'inherit'
  });

  if (result.status !== 0) {
    throw new Error('Failed to generate wrapper artifacts for test coverage');
  }
}

function expectFile(pathname, minSize = 1) {
  const exists = fs.existsSync(pathname);
  expect(exists).toBe(true);
  const { size } = fs.statSync(pathname);
  expect(size).toBeGreaterThanOrEqual(minSize);
}

describe('npm package artifacts', () => {
  beforeAll(() => {
    ensureBuilt();
  });

  test('ships the Node-specific Musashi build', () => {
    expectFile(path.join(pkgRoot, 'musashi-node.out.mjs'));
    expectFile(path.join(pkgRoot, 'musashi-node.out.wasm'), MIN_WASM_SIZE);

    const mapPath = path.join(pkgRoot, 'musashi-node.out.wasm.map');
    if (fs.existsSync(mapPath)) {
      expectFile(mapPath);
    }
  });

  test('advertises the fusion entrypoints in package.json', () => {
    const packageJsonPath = path.join(pkgRoot, 'package.json');
    const packageJson = JSON.parse(fs.readFileSync(packageJsonPath, 'utf8'));

    expect(packageJson.files).toEqual(
      expect.arrayContaining([
        'musashi-node.out.mjs',
        'musashi-node.out.wasm',
        'musashi-node.d.ts'
      ])
    );

    expect(packageJson.exports).toMatchObject({
      './node': { import: './musashi-node.out.mjs', types: './musashi-node.d.ts' },
      './core': { import: './lib/core/index.js', types: './lib/core/index.d.ts' },
      './core/*': { import: './lib/core/*.js', types: './lib/core/*.d.ts' },
      './musashi-node.out.mjs': './musashi-node.out.mjs',
      './musashi-node.out.wasm': './musashi-node.out.wasm'
    });
  });

  test('ships the core wrapper runtime', () => {
    [
      'index.js',
      'index.d.ts',
      'musashi-wrapper.js',
      'musashi-wrapper.d.ts',
      'types.js',
      'types.d.ts'
    ].forEach((filename) => {
      expect(fs.existsSync(path.join(coreDir, filename))).toBe(true);
    });

    expect(fs.existsSync(path.join(coreWasmDir, 'musashi-node-wrapper.mjs'))).toBe(true);
    expect(fs.existsSync(path.join(coreWasmDir, 'musashi-node.out.mjs'))).toBe(true);
  });

  test('keeps browser loaders alongside the Node build', () => {
    ['musashi-loader.mjs', 'musashi.wasm'].forEach((filename) => {
      expect(fs.existsSync(path.join(distDir, filename))).toBe(true);
    });

    ['musashi-perfetto-loader.mjs', 'musashi-perfetto.wasm'].forEach((filename) => {
      const fullPath = path.join(distDir, filename);
      if (!fs.existsSync(fullPath)) {
        return;
      }

      const minSize = filename.endsWith('.wasm') ? MIN_WASM_SIZE : 1;
      expectFile(fullPath, minSize);
    });
  });
});
