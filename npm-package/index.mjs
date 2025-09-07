import createModule from "./dist/musashi-loader.mjs";

export default async function init(options = {}) {
  const isNode = typeof process !== "undefined" && !!process.versions?.node;
  const wasmUrl = new URL("./dist/musashi.wasm", import.meta.url);
  
  let wasmPath;
  if (isNode) {
    const { fileURLToPath } = await import("url");
    wasmPath = fileURLToPath(wasmUrl);
  } else {
    wasmPath = wasmUrl.href;
  }
  
  const locateFile = (p) => p.endsWith(".wasm") ? wasmPath : p;
  return createModule({ locateFile, ...options });
}