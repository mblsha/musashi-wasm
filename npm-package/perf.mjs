import createModule from "./dist/musashi-perfetto-loader.mjs";

export default function init(options = {}) {
  const isNode = typeof process !== "undefined" && !!process.versions?.node;
  const wasmUrl = new URL("./dist/musashi-perfetto.wasm", import.meta.url);
  
  // Browser-compatible path handling
  let wasmPath;
  if (isNode) {
    // Only import fileURLToPath in Node.js environments
    // Using dynamic import to avoid top-level import that breaks browsers
    return import("url").then(({ fileURLToPath }) => {
      wasmPath = fileURLToPath(wasmUrl);
      const locateFile = (p) => p.endsWith(".wasm") ? wasmPath : p;
      return createModule({ locateFile, ...options });
    });
  } else {
    // Browser environment - use URL directly
    wasmPath = wasmUrl.href;
    const locateFile = (p) => p.endsWith(".wasm") ? wasmPath : p;
    return createModule({ locateFile, ...options });
  }
}