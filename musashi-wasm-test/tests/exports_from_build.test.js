import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

// Use the same loader the other tests rely on
import createMusashiModule from '../load-musashi.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Resolve important paths relative to this test file
const repoRoot = path.resolve(__dirname, '..', '..');
const buildScriptPath = path.resolve(repoRoot, 'build.sh');
const nodeWasmPath = path.resolve(repoRoot, 'musashi-node.out.mjs');

// Small helper to extract clean symbol tokens from a block body
function parseSymbolBlock(body) {
  return body
    .split('\n')
    .map((line) => line.replace(/#.*/, '').trim()) // strip comments and whitespace
    .filter((line) => line.length > 0)
    .map((line) => line.replace(/^['\"]|['\"]$/g, '')); // remove surrounding quotes if any
}

function parseExportedFunctionsFromBuild(buildShContent) {
  const base = [];
  const perfettoExtras = [];

  // Match both the base assignment and any subsequent "+=" append blocks
  // Capture the operator (= vs +=) and the block content between parentheses
  const re = /exported_functions\s*(\+?=)\s*\(([^)]*)\)/gms;
  let m;
  while ((m = re.exec(buildShContent)) !== null) {
    const op = m[1];
    const body = m[2] || '';
    const symbols = parseSymbolBlock(body);
    if (op === '=') base.push(...symbols);
    else perfettoExtras.push(...symbols);
  }

  return { base, perfettoExtras };
}

describe('Export parity with build.sh', () => {
  let Module;

  beforeAll(async () => {
    // Ensure artifacts exist to provide clearer failure messages
    if (!fs.existsSync(buildScriptPath)) {
      // Fail fast if repository layout is unexpected
      throw new Error(`build.sh not found at ${buildScriptPath}`);
    }
    if (!fs.existsSync(nodeWasmPath)) {
      // Keep message short; the existing integration test already prints guidance
      throw new Error(`WASM module not found at ${nodeWasmPath}. Run ./build.sh first.`);
    }

    Module = await createMusashiModule();
    expect(Module).toBeDefined();
  });

  it('exports all functions declared in build.sh', async () => {
    const buildContent = fs.readFileSync(buildScriptPath, 'utf8');
    const { base, perfettoExtras } = parseExportedFunctionsFromBuild(buildContent);

    // Decide whether to include Perfetto-only exports.
    // Prefer explicit env signal (mirrors build.sh), with a runtime fallback.
    const envPerfetto = process.env.ENABLE_PERFETTO === '1';
    const runtimePerfetto = perfettoExtras.some((name) => typeof Module[name] === 'function');
    const includePerfetto = envPerfetto || runtimePerfetto;

    const expected = includePerfetto ? [...base, ...perfettoExtras] : base;

    // Validate that each expected symbol is present as a function on the Module.
    // Collect any missing to produce a single, informative assertion.
    const missing = [];
    for (const name of expected) {
      const val = Module[name];
      if (typeof val !== 'function') {
        missing.push(name);
      }
    }

    if (missing.length > 0) {
      // Provide a compact failure message with the missing symbols
      throw new Error(
        `Missing ${missing.length} exported functions from Module: ${missing.join(', ')}`
      );
    }
  });
});

