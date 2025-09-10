Title: Add safe entry + stop-pc API and early tracing to musashi-wasm (WASM wrapper)

Summary

This patchset adds small, optional APIs to the musashi-wasm wrapper that make
embedding more robust and easier to debug:

- Safe entry API: _set_entry_point(pc) now performs a soft reset and then
  sets SR/IRQ/VBR and PC to the requested address. This avoids prefetch/flag
  ambiguity at first user fetch.
- Built-in stop-pc: _set_stop_pc(addr)/_clear_stop_pc() stops the timeslice as
  soon as PC == addr, without requiring a JS hook. This enables robust
  “call-until-RTS” flows by writing a RAM sentinel as the return address.
- Early instruction trace (optional): _early_trace_enable/reset/count/pc/ir
  to capture the first N (pc, ir) pairs before other side effects. Useful to
  analyze very early divergence.
- Optional diagnostic guard: _enable_bios_nop_mapping(enable) temporarily maps
  0xC00000..0xC20000 reads to NOPs to avoid OOB when running without BIOS.
  This is purely for diagnostics; off by default.

Rationale

Several host environments want to execute helper functions from ROM (e.g.,
init routines) and stop as soon as they return. Without a safe return sentinel
and a timeslice stop at that PC, execution may fall through to address 0,
decode vector-table bytes as opcodes, and vector to BIOS, which is either
unmapped or handled differently on the host.

The stop-pc API provides a minimal, well-contained way to stop at a known
address and hand control back to the host without requiring a JS hook or
custom asm. The soft set_entry_point(pc) keeps first fetch deterministic.

Changes (wrapper-only)

- myfunc.cc
  - Add early trace ring and exports: _early_trace_enable, _early_trace_reset,
    _early_trace_count, _early_trace_pc, _early_trace_ir
  - Add _set_stop_pc/_clear_stop_pc; check it in m68k_instruction_hook_wrapper
    and call m68k_end_timeslice() + return 1 if matched
  - Add _enable_bios_nop_mapping (diagnostic only; off by default)
  - Strengthen _set_entry_point(pc) to call m68k_pulse_reset(), then set
    SR=0x2700, clear IRQ, VBR=0, and set PC

- build.fish
  - Export new functions in EXPORTED_FUNCTIONS for all ESM outputs

Compatibility

- All new features are optional; default behavior remains unchanged unless
  the new functions are called by the host.
- No changes to the core musashi engine.

Example host usage

1) Safe call to ROM function at 0x20e2 that returns via RTS:

   // Choose a RAM sentinel (word-aligned) and set built-in stop pc
   module._set_stop_pc(0x100200);
   // Write the sentinel at [SP]
   const sp = module._m68k_get_reg(0, /*M68K_REG_SP*/ 15);
   // host writes 0x100200 at SP (big-endian)
   // then set entry point and execute a slice
   module._set_entry_point(0x20e2);
   module._m68k_execute(10000);

2) Capture earliest instructions for diagnostics:

   module._early_trace_enable(32);
   module._set_entry_point(0x410);
   module._m68k_execute(10000);
   const n = module._early_trace_count();
   for (let i = 0; i < n; i++) console.log(module._early_trace_pc(i), module._early_trace_ir(i));

3) Temporarily allow vector jumps into 0xC0xxxx without an OOB (diagnostic):

   module._enable_bios_nop_mapping(1);
   // run; NOPs are returned for reads in 0xC00000..0xC20000
   module._enable_bios_nop_mapping(0);

Test plan

- Unit-style Node harnesses exercise: _set_entry_point + _set_stop_pc to run
  small helper calls safely; early trace APIs produce first few (pc, ir);
  bios-nop mapping only used for diagnostics.
- Existing wasm artifacts load and run without requiring host changes.

Notes

- These APIs are wrapper-level and avoid touching the musashi core, keeping
  portability and behavior intact. They help hosts avoid accidental first-step
  vectoring and make early CPU behavior easier to analyze.

