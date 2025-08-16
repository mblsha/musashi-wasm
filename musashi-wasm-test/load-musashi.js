// ESM loader for Musashi module
// Imports the ESM factory built at repo root by build_wasm_simple.sh
export default async function createMusashiModule() {
  const { default: createMusashi } = await import('../musashi-node.out.mjs');
  return createMusashi({});
}
