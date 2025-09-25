// ESM wrapper for the Musashi WASM module
// This file loads the actual ESM module that should be copied here by the CI

import createMusashi from 'musashi-wasm/node';

export default createMusashi;