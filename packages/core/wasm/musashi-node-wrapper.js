// packages/core/wasm/musashi-node-wrapper.js
'use strict';

const fs = require('fs');
const path = require('path');
const { pathToFileURL } = require('url');

module.exports = async function createMusashiModule(options = {}) {
  const modulePath = path.resolve(__dirname, '../../../musashi-node.out.js');

  if (!fs.existsSync(modulePath)) {
    throw new Error(`Musashi WASM module not found at ${modulePath}. Please run 'make wasm'.`);
  }

  let exported;
  try {
    // Prefer CommonJS require when available (most Emscripten Node builds)
    exported = require(modulePath);
  } catch (requireErr) {
    // Fallback for ESM environments (or if the glue is flagged ESM)
    const ns = await import(pathToFileURL(modulePath).href);
    exported = (ns && (ns.default || ns));
  }

  // If it's already a Module or a thenable, just return/await it.
  if (typeof exported !== 'function') {
    return await Promise.resolve(exported);
  }

  // Typical Emscripten Node glue: factory(options) -> Promise<Module>
  const baseDir = path.dirname(modulePath);
  const Module = await exported({
    locateFile: (p) => path.join(baseDir, p),
    ...options,
  });

  return Module;
};