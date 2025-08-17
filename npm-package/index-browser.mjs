// Minimal browser entry: reuse the main ESM initializer which already handles
// browser vs node via isNode check and locateFile hook.
import init from './index.mjs';
export default init;

