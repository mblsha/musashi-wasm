// ESM wrapper for the Musashi WASM module
// Prefer local build from repository root if present (developer-friendly),
// otherwise fall back to a colocated build.

let createMusashi;
try {
  // In this repo, CI and local builds place outputs at repo root
  // third_party/musashi-wasm/musashi-node.out.mjs
  // Resolve relative to this file's location.
  createMusashi = (await import('../../../musashi-node.out.mjs')).default;
} catch (_e) {
  // Fallback to colocated wrapper (if CI copied files here)
  createMusashi = (await import('./musashi-node.out.mjs')).default;
}

export default createMusashi;
