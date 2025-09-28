import { createSystem } from '../../lib/core/index.js';

const statusNode = document.getElementById('status');
if (!(statusNode instanceof HTMLElement)) {
  throw new Error('status element missing');
}
const statusEl = statusNode;

async function run() {
  const rom = new Uint8Array(0x2000);
  try {
    await createSystem({ rom, ramSize: 0x2000 });
    statusEl.textContent = 'createSystem: ok';
  } catch (error) {
    console.error('createSystem failed', error);
    statusEl.textContent = `error: ${error instanceof Error ? error.message : String(error)}`;
  }
}

run();
