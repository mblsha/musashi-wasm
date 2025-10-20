export default async function initPerfettoStub() {
  const heapU8 = new Uint8Array(0);
  const heap32 = new Int32Array(0);
  const heapU32 = new Uint32Array(0);

  return {
    _m68k_init() {},
    _m68k_set_trace_mem_callback() {},
    _m68k_trace_enable() {},
    _m68k_trace_set_mem_enabled() {},
    _clear_regions() {},
    _clear_pc_hook_addrs() {},
    _set_read_mem_func() {},
    _set_write_mem_func() {},
    _set_pc_hook_func() {},
    _m68k_pulse_reset() {},
    _m68k_get_reg() {
      return 0;
    },
    _malloc() {
      return 0;
    },
    _free() {},
    addFunction() {
      return 1;
    },
    removeFunction() {},
    HEAPU8: heapU8,
    HEAP32: heap32,
    HEAPU32: heapU32,
    _m68k_perfetto_init() {
      return 0;
    },
    _m68k_fault_clear() {},
    _m68k_fault_record_ptr() {
      return 0;
    },
  };
}
