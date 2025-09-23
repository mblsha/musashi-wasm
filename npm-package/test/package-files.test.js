const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const pkgRoot = path.join(__dirname, '..');
const distDir = path.join(pkgRoot, 'dist');

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

  test('advertises the Node entrypoints in package.json', () => {
    const packageJsonPath = path.join(pkgRoot, 'package.json');
    const packageJson = JSON.parse(fs.readFileSync(packageJsonPath, 'utf8'));

    expect(packageJson.files).toEqual(
      expect.arrayContaining([
        'musashi-node.out.mjs',
        'musashi-node.out.wasm'
      ])
    );

    expect(packageJson.exports).toMatchObject({
      './node': { import: './musashi-node.out.mjs' },
      './musashi-node.out.mjs': './musashi-node.out.mjs',
      './musashi-node.out.wasm': './musashi-node.out.wasm'
    });
  });

  test('keeps browser loaders alongside the Node build', () => {
    const expectedDistFiles = ['musashi-loader.mjs', 'musashi.wasm'];
    expectedDistFiles.forEach((filename) => {
      expect(fs.existsSync(path.join(distDir, filename))).toBe(true);
    });
  });
});
