// Node.js wrapper for the Musashi WASM module
// This handles the loading of the module for Node.js environments

const fs = require('fs');
const path = require('path');
const vm = require('vm');

module.exports = function createMusashiModule() {
  // Read the module file
  const modulePath = path.resolve(__dirname, '../../../musashi-node.out.js');
  
  if (!fs.existsSync(modulePath)) {
    throw new Error(`Musashi WASM module not found at ${modulePath}. Please run 'make wasm' to build it.`);
  }
  
  let moduleCode = fs.readFileSync(modulePath, 'utf8');

  // Remove the problematic ES6 export at the end if it exists
  moduleCode = moduleCode.replace(/^export\s+function\s+getModule\(\)\s*{[^}]*}/gm, '');

  // Create a new context and evaluate the code
  const sandbox = {
    module: { exports: {} },
    exports: {},
    require: require,
    __dirname: path.dirname(modulePath),
    __filename: modulePath,
    console: console,
    process: process,
    Buffer: Buffer,
    setTimeout: setTimeout,
    setInterval: setInterval,
    clearTimeout: clearTimeout,
    clearInterval: clearInterval,
    global: global
  };

  // Run the module code in the sandbox
  vm.runInNewContext(moduleCode, sandbox, {
    filename: modulePath,
    timeout: 10000
  });

  // Return the module factory function
  const moduleFactory = sandbox.module.exports || sandbox.exports;
  
  // The Emscripten module returns a factory function that we need to call
  if (typeof moduleFactory === 'function') {
    return moduleFactory;
  } else if (typeof moduleFactory.default === 'function') {
    return moduleFactory.default;
  } else {
    throw new Error('Unable to find Musashi module factory function');
  }
};