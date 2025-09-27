import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, rmSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const musashiVersion = process.env.MUSASHI_WASM_REPRO_VERSION ?? '0.1.14';
const workDir = mkdtempSync(join(tmpdir(), 'musashi-wasm-repro-'));

const packageJson = {
  name: 'musashi-wasm-fusion-divergence',
  private: true,
  type: 'module',
  dependencies: {
    'musashi-wasm': musashiVersion,
  },
  scripts: {
    repro: 'node repro.mjs',
  },
};

const reproTemplatePath = new URL('assets/fusion_repro_template.mjs', import.meta.url);
const reproSource = readFileSync(reproTemplatePath, 'utf8');

writeFileSync(join(workDir, 'package.json'), JSON.stringify(packageJson, null, 2));
writeFileSync(join(workDir, 'repro.mjs'), reproSource);

try {
  execFileSync('npm', ['install'], { cwd: workDir, stdio: 'inherit', timeout: 60_000 });
  const output = execFileSync('npm', ['run', 'repro', '--silent'], { cwd: workDir, encoding: 'utf8', timeout: 60_000 });
  const lines = output.trim().split('\n');
  const lastLine = lines[lines.length - 1];
  const parsed = JSON.parse(lastLine);
  assert.equal(parsed.ok, true, 'expected repro script to observe divergence');
  const extra = parsed.divergence.tpEvents.find((evt) => evt.bytes.includes('0x100a80:00'));
  assert(extra, 'expected tp/core events to include byte write at (A0)');
  console.log('⚠️  Published musashi-wasm still diverges:');
  console.log(JSON.stringify(parsed.divergence, null, 2));
} finally {
  rmSync(workDir, { recursive: true, force: true });
}
