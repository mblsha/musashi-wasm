// ESM wrapper for the Musashi WASM module with a safe fallback.
// Primary path: local CI drops musashi-node.out.mjs alongside this file.
// Fallback: use repository-level musashi-node.mjs if present.

export default async function createMusashiModule() {
  try {
    const mod = await import('./musashi-node.out.mjs');
    return mod.default();
  } catch (_e) {
    const fallback = await import('../../../musashi-node.mjs');
    return fallback.default();
  }
}
