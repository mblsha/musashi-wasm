// pre.js
// Define minimal setjmp/longjmp shims before module instantiation so that
// env imports are satisfied at link/load time. These no-ops are acceptable
// for our usage because the core is not expected to actually longjmp during
// normal execution paths with our memory bridge.

if (typeof saveSetjmp === 'undefined') {
  var saveSetjmp = function () { return 0; };
}
if (typeof testSetjmp === 'undefined') {
  var testSetjmp = function () { return 0; };
}
if (typeof emscripten_longjmp === 'undefined') {
  var emscripten_longjmp = function () { /* no-op */ };
}

