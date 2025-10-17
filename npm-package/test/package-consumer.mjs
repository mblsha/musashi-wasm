import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { randomUUID } from 'node:crypto';
import { spawn } from 'node:child_process';

const pkgDir = new URL('..', import.meta.url);

const run = (cmd, args, options = {}) =>
  new Promise((resolve, reject) => {
    const child = spawn(cmd, args, {
      stdio: ['ignore', 'pipe', 'inherit'],
      ...options,
    });

    let stdout = '';
    if (child.stdout) {
      child.stdout.setEncoding('utf8');
      child.stdout.on('data', (chunk) => {
        stdout += chunk;
      });
    }

    child.on('error', reject);
    child.on('close', (code) => {
      if (code === 0) {
        resolve(stdout);
      } else {
        reject(new Error(`${cmd} ${args.join(' ')} exited with ${code}`));
      }
    });
  });

const pkgPath = (relative) => new URL(relative, pkgDir).pathname;

const ensurePerfBuild = async () => {
  await run(
    'npm',
    ['run', 'build'],
    {
      cwd: pkgPath('.'),
      env: { ...process.env, ENABLE_PERFETTO: '1' },
    }
  );
};

const packPackage = async () => {
  const output = await run(
    'npm',
    ['pack', '--json'],
    { cwd: pkgPath('.') }
  );
  const entries = JSON.parse(output);
  if (!Array.isArray(entries) || entries.length === 0) {
    throw new Error(`Unexpected npm pack output: ${output}`);
  }
  return join(pkgPath('.'), entries[0].filename);
};

const runConsumerTest = async (tarballPath) => {
  const tempDir = await mkdtemp(join(tmpdir(), 'musashi-perf-consumer-'));
  const cleanup = async () => {
    await rm(tempDir, { recursive: true, force: true }).catch(() => {});
  };

  try {
    await run('npm', ['init', '-y'], { cwd: tempDir });
    await run('npm', ['install', tarballPath], { cwd: tempDir });

    const testScript = `
      import { createSystem } from 'musashi-wasm/core';
      const rom = new Uint8Array(0x2000);
      rom.set([0x00,0x10,0x00,0x00, 0x00,0x00,0x04,0x00], 0);
      rom.set([0x4e,0x71,0x4e,0x71,0x4e,0x75], 0x400);
      const system = await createSystem({ rom, ramSize: 0x2000 });
      if (!system.tracer.isAvailable()) {
        throw new Error('Perfetto tracer not available in packaged module');
      }
      system.tracer.start({ instructions: true });
      system.run(4);
      const trace = system.tracer.stop();
      if (!(trace instanceof Uint8Array) || trace.length === 0) {
        throw new Error('Perfetto trace missing or empty');
      }
      system.cleanup();
      console.log('TRACE_LENGTH=' + trace.length);
    `;

    const scriptFile = join(tempDir, `verify-${randomUUID()}.mjs`);
    await writeFile(scriptFile, testScript, 'utf8');
    const output = await run('node', [scriptFile], { cwd: tempDir });
    if (!/TRACE_LENGTH=\d+/.test(output)) {
      throw new Error(`Unexpected verification output: ${output}`);
    }
  } finally {
    await cleanup();
  }
};

const main = async () => {
  await ensurePerfBuild();
  const tarball = await packPackage();
  try {
    await runConsumerTest(tarball);
  } finally {
    await rm(tarball, { force: true }).catch(() => {});
  }
};

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
