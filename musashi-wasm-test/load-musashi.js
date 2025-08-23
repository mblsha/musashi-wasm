// ESM loader for Musashi module
// Imports the ESM factory built at repo root by build.fish/build_wasm_simple.sh
export default async function createMusashiModule() {
  // Try Node-specific build first, then fall back to universal build
  const candidates = [
    '../musashi-node.out.mjs',
    '../musashi-universal.out.mjs',
  ];
  let lastErr;
  for (const rel of candidates) {
    try {
      const { default: createMusashi } = await import(rel);
      return createMusashi({});
    } catch (err) {
      lastErr = err;
    }
  }
  throw lastErr || new Error('Failed to load Musashi WASM module');
}
