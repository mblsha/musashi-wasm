# `system.call` F-line Trap Timeout Investigation

## Background
- Recent changes taught `System#step()` to run the trap handler for an F-line (vector 11) exception inside a single step. The change works: a single `step()` now produces the expected write to `0x100B1C`.
- To guard against regressions, we introduced `packages/core/src/step-trap.test.ts`. The test programs a trap handler at `0x400`, installs the F-line opcode at `0x5DC20`, and asserts that a single `step()` emits the expected memory write.
- While validating the suite, `src/index.test.ts` (`should invoke onMemoryRead/Write with PC context`) now **times out** when executed under a 30 s timeout wrapper. Every other Jest case finishes in under ~1.5 s, so the timeout is specific to this scenario.

## Minimal Reproduction
```bash
# From packages/core
NODE_OPTIONS='--experimental-vm-modules' timeout 30 \
  npx jest --runTestsByPath src/index.test.ts \
           --testNamePattern='should invoke onMemoryRead/Write with PC context'
```
Result: exit code 124 after ~30 s; no Jest output beyond the warning about experimental VM modules.

### Inline script sanity check
```bash
timeout 30 node --input-type=module <<'NODE'
import { createSystem } from 'musashi-wasm';
const rom = new Uint8Array(0x4000);
const sys = await createSystem({ rom, ramSize: 0x20000 });
const sub = new Uint8Array([
  0x20,0x3c,0xca,0xfe,0xba,0xbe, // move.l #$CAFEBABE, D0
  0x2f,0x00,                     // move.l D0, -(SP)
  0x22,0x1f,                     // move.l (SP)+, D1
  0x4e,0x75                      // rts
]);
sys.writeBytes(0x600, sub);
sys.setRegister('sp', 0x100004);
const writes = [], reads = [];
sys.onMemoryWrite(ev => writes.push(ev));
sys.onMemoryRead(ev => reads.push(ev));
await sys.call(0x600);
console.log({ writes, reads, pc: sys.getRegisters().pc.toString(16) });
NODE
```
This script also times out (exit 124). No memory events are emitted before the timeout.

## Observations
- The timeout occurs specifically for `system.call(...)` when the callee installs memory read/write hooks. Without the hooks—or when using `system.step()` instead—execution returns promptly.
- The watchdog (JS hook) returns immediately, so the C++ sentinel should be triggered. However, the session never reports `_exec_session.done = true`; the loop in `m68k_call_until_js_stop` continues to consume timeslice chunks until the external timeout fires.
- The trap-focused step test *does* finish quickly (around 200 ms) and logs the expected memory write.

## Hypothesis
`m68k_call_until_js_stop` relies on the JS hook (or sentinel address) to break out. The F-line step change adjusted hook behavior and may have altered how sentinel bookkeeping works when the JS memory hooks fire. If `_exec_session.done` is left `false`, the loop never terminates and we loop until the outer timeout kills the process.

## Questions for Reviewers
1. Does the sentinel session still receive the expected PC when the memory hook fires? (We might need to update `_exec_session.done` for memory hooks similar to how we do for instruction/probe hooks.)
2. Should we switch the memory-hook test from `call()` to explicit `step()` calls to avoid depending on sentinel semantics?
3. Is there an established pattern for combining `call()` with memory read/write tracing that we can copy instead of reimplementing?

## Next Steps Under Consideration
- Instrument the C++ bridge (`processHooks`, `my_instruction_hook_function`) to log/set `_exec_session.done` whenever `js_write_callback` fires during a call session.
- As a short-term guard, reduce the test to a bounded number of `step()` calls (still verifying the write/read events) so we keep coverage without depending on sentinel exit.

Feedback welcomed—particularly on whether the sentinel exit should be triggered by the memory hook path or if we should avoid `call()` entirely for this coverage.
