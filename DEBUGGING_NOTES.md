Musashi WASM Debugging Notes (vendored core)

Context
- This fork is consumed by a parent repo which runs a “fusion” lock‑step comparison between its local Musashi and this third_party build.
- Fusion compares full CPU state and per‑instruction memory ops step‑by‑step, so hook timing and fetch order must be deterministic.

Key Changes in This Fork
1) Deterministic hook order (start + end boundary):
   - m68kcpu.c: call `m68k_instruction_end_boundary_hook(post_pc)` immediately after executing each instruction and before fetching the next IR.
   - myfunc.cc: implement `m68k_instruction_end_boundary_hook` that invokes the JS pc_hook and ends the timeslice if requested.
   - Result: the previous artifact (next‑opcode fetch included in the prior step) is gone; post‑PC matches the local backend at 0x410/0x20e2.

2) Single path for instruction/immediate/PC‑relative reads:
   - m68k_memory_bridge.cc: all reads funnel through the same region‑aware bridge, using `my_read_memory` for consistent big‑endian composition and tracing. This avoids double‑composition bugs and ensures uniform tracing semantics.

3) Clean entry without BIOS vectors in WASM:
   - myfunc.cc: `set_entry_point(pc)` sets SR=0x2700, clears IRQ, sets VBR=0 (68000), then jumps to `pc`. This prevents early vectoring into BIOS space if no BIOS is mapped.
   - Optional: `enable_bios_nop_mapping(1)` can map BIOS vector fetches to NOPs for diagnostics.

4) Increased WASM stack for heavy tracing:
   - build.fish: add `-s STACK_SIZE=262144`. Heavy JSR tracing previously triggered stack overflow under default 64KB.

Troubleshooting
- “Extra” read at PC+N in fusion traces:
  - Fixed by the end‑boundary hook. If you see it again, ensure the end‑boundary runs before the next IR fetch and that start‑boundary also triggers (two deterministic points per instruction).

- Early BIOS jumps or illegal reads:
  - When running without a BIOS in WASM, do not rely on `m68k_pulse_reset()` vectors for entry. Use `set_entry_point(pc)`.
  - For diagnostics only, `enable_bios_nop_mapping(1)` masks BIOS fetches.

- Address domain:
  - 68000 uses 24‑bit addressing (CPU_ADDRESS_MASK=0x00FFFFFF); all memory bridges mask addresses to 24‑bit before access.

Build
- Node ESM: `musashi-node.out.mjs`
- Web ESM: `musashi.out.mjs`
- Universal ESM: `musashi-universal.out.mjs`
- All use: `-s MODULARIZE=1 -s EXPORT_ES6=1 -s WASM=1 -s WASM_ASYNC_COMPILATION=1`
- Stack: `-s STACK_SIZE=262144`

Exports of interest (JS):
- `_set_entry_point`, `_set_pc_hook_func`, `_add_pc_hook_addr`, `_enable_bios_nop_mapping`
- Early trace/rings: `_m68k_trace_*`, `_flow_trace_*`, `_mem_trace_*`

