#!/usr/bin/env node
// Run a command and forcibly terminate it after N seconds.
// Usage: node scripts/run-with-timeout.mjs <seconds> <cmd> [...args]

import { spawn, execFile } from 'node:child_process';

function die(msg, code = 1) {
  console.error(msg);
  process.exit(code);
}

const [,, secsArg, cmd, ...args] = process.argv;
if (!secsArg || !cmd) {
  die('Usage: run-with-timeout <seconds> <cmd> [...args]');
}

const seconds = Number(secsArg);
if (!Number.isFinite(seconds) || seconds <= 0) {
  die(`Invalid seconds: ${secsArg}`);
}

const isWindows = process.platform === 'win32';
// Detach to create a new process group so we can kill the entire tree (POSIX)
const child = spawn(cmd, args, { stdio: 'inherit', detached: !isWindows, shell: false });

let timedOut = false;
const killTree = () => {
  if (isWindows) {
    // /T to kill the process tree; /F to force
    execFile('taskkill', ['/PID', String(child.pid), '/T', '/F'], (err) => {
      if (err) {
        console.error(`taskkill failed: ${err.message}`);
      }
    });
    return;
  }
  try { process.kill(-child.pid, 'SIGKILL'); } catch {}
  try { child.kill('SIGKILL'); } catch {}
  try { process.kill(-child.pid, 'SIGTERM'); } catch {}
  try { child.kill('SIGTERM'); } catch {}
};

const timer = setTimeout(() => {
  timedOut = true;
  console.error(`\n⏱️  Timeout (${seconds}s): killing '${cmd} ${args.join(' ')}' (pid ${child.pid})`);
  killTree();
}, seconds * 1000);

child.on('exit', (code, signal) => {
  clearTimeout(timer);
  if (timedOut) {
    process.exit(124); // conventional timeout exit code
  }
  if (signal) {
    console.error(`Process terminated by signal: ${signal}`);
    process.exit(128);
  }
  process.exit(code ?? 0);
});

child.on('error', (err) => {
  clearTimeout(timer);
  console.error(`Failed to start process: ${err.message}`);
  process.exit(2);
});
