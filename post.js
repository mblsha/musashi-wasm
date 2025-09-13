// post.js
// Provide minimal setjmp/longjmp shims for environments where the
// JS library implementations are not linked by default. These are
// no-ops sufficient for runtimes that never actually longjmp during
// normal execution (our memory bridge avoids bus/address errors).
//
// Note: These are best-effort fallbacks; proper setjmp/longjmp emulation
// should be preferred if the core relies on it for control flow.
Module.saveSetjmp = Module.saveSetjmp || function () { return 0; };
Module.testSetjmp = Module.testSetjmp || function () { return 0; };
Module.emscripten_longjmp = Module.emscripten_longjmp || function () { /* no-op */ };

