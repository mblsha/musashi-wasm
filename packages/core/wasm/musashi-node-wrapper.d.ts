// Type declarations for the Musashi WASM module loader
declare const createMusashi: () => Promise<import('../src/musashi-wrapper').MusashiEmscriptenModule>;
export default createMusashi;
