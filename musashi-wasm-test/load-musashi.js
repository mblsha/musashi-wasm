// Wrapper to load the Musashi module safely
const fs = require('fs');
const path = require('path');
const vm = require('vm');

// Read the module file
const modulePath = path.resolve(__dirname, '../musashi-node.out.js');
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
    timeout: 5000
});

// Export what the module exported
module.exports = sandbox.module.exports || sandbox.exports;