#!/usr/bin/env bash
set -euo pipefail

die() { echo "Error: $*" >&2; exit 1; }

run() { echo; echo ">>> $*"; if ! "$@"; then code=$?; die "FAILED ($code): $*"; fi; }

echo "==== TOOLCHAIN VERIFICATION (bash) ===="

# Ensure emcc is available. Prefer EMSDK if provided, else rely on PATH.
if [[ -n "${EMSDK:-}" ]]; then
  echo "Found EMSDK: $EMSDK"
  if [[ -x "$EMSDK/upstream/emscripten/emcc" ]]; then
    export PATH="$EMSDK/upstream/emscripten:$EMSDK:$PATH"
  elif [[ -x "$EMSDK/emcc" ]]; then
    # Homebrew layout
    export PATH="$EMSDK:$PATH"
  fi
else
  echo "EMSDK not set; will use emcc from PATH if available"
fi

if ! command -v emcc >/dev/null 2>&1; then
  die "emcc not found. Install Emscripten SDK or place emcc in PATH"
fi

echo "emcc: $(command -v emcc)"
if command -v node >/dev/null 2>&1; then
  echo "node: $(command -v node)"
  echo "emcc version:"; emcc --version | head -n 1
  echo "node version:"; node --version
else
  echo "node: (not found)"
  echo "emcc version:"; emcc --version | head -n 1
fi
echo "================================"

ENABLE_PERFETTO_FLAG="${ENABLE_PERFETTO:-0}"
if [[ "$ENABLE_PERFETTO_FLAG" == "1" ]]; then
  echo "Building with Perfetto tracing enabled..."
  export ENABLE_PERFETTO=1
else
  echo "Building without Perfetto tracing (set ENABLE_PERFETTO=1 to enable)..."
fi

# Build C/C++ object files first (uses Makefile)
run emmake make -j8 ENABLE_PERFETTO="$ENABLE_PERFETTO_FLAG"

# Exported functions (C symbols must be prefixed with underscore)
# IMPORTANT: keep this list sorted lexicographically; one symbol per line.
exported_functions=(
  _add_pc_hook_addr
  _add_region
  _clear_instr_hook_func
  _clear_pc_hook_addrs
  _clear_pc_hook_func
  _clear_registered_names
  _clear_regions
  _enable_printf_logging
  _free
  _get_a_reg
  _get_d_reg
  _get_function_name
  _get_memory_name
  _get_pc_reg
  _get_sp_reg
  _get_sr_reg
  _malloc
  _m68k_call_until_js_stop
  _m68k_cycles_run
  _m68k_disassemble
  _m68k_end_timeslice
  _m68k_execute
  _m68k_get_last_break_reason
  _m68k_get_reg
  _m68k_get_total_cycles
  _m68k_init
  _m68k_pulse_reset
  _m68k_regnum_from_name
  _m68k_reset_last_break_reason
  _m68k_reset_total_cycles
  _m68k_set_context
  _m68k_set_reg
  _m68k_set_trace_flow_callback
  _m68k_set_trace_instr_callback
  _m68k_set_trace_mem_callback
  _m68k_step_one
  _m68k_trace_add_mem_region
  _m68k_trace_clear_mem_regions
  _m68k_trace_enable
  _m68k_trace_is_enabled
  _m68k_trace_set_flow_enabled
  _m68k_trace_set_instr_enabled
  _m68k_trace_set_mem_enabled
  _my_initialize
  _perfetto_destroy
  _perfetto_enable_flow
  _perfetto_enable_instructions
  _perfetto_enable_memory
  _perfetto_export_trace
  _perfetto_free_trace_data
  _perfetto_init
  _perfetto_is_initialized
  _perfetto_save_trace
  _register_function_name
  _register_memory_name
  _register_memory_range
  _reset_myfunc_state
  _set_a_reg
  _set_d_reg
  _set_entry_point
  _set_full_instr_hook_func
  _set_isp_reg
  _set_pc_hook_func
  _set_pc_reg
  _set_probe_callback
  _set_read8_callback
  _set_read_mem_func
  _set_sr_reg
  _set_usp_reg
  _set_write8_callback
  _set_write_mem_func
)

# Add Perfetto-only exports when enabled (parity with Fish)
if [[ "$ENABLE_PERFETTO_FLAG" == "1" ]]; then
  exported_functions+=(
    _m68k_perfetto_cleanup_slices
    _m68k_perfetto_destroy
    _m68k_perfetto_enable_flow
    _m68k_perfetto_enable_instructions
    _m68k_perfetto_enable_memory
    _m68k_perfetto_export_trace
    _m68k_perfetto_free_trace_data
    _m68k_perfetto_init
    _m68k_perfetto_is_initialized
    _m68k_perfetto_save_trace
  )
fi

# Runtime function includes (need $-prefixed names)
default_lib_funcs=(
  '$addFunction' '$removeFunction' '$ccall' '$cwrap' '$getValue'
  '$setValue' '$UTF8ToString' '$stringToUTF8' '$writeArrayToMemory'
)

runtime_methods=(
  HEAP8 HEAPU8 HEAP16 HEAPU16 HEAP32 HEAPU32 HEAPF32 HEAPF64
  addFunction removeFunction ccall cwrap getValue setValue UTF8ToString
  stringToUTF8 writeArrayToMemory
)

to_ems_list() {
  # Print a JS array literal from positional args
  local first=1 out="["
  for item in "$@"; do
    if [[ $first -eq 1 ]]; then first=0; else out+=", "; fi
    # Quote strings with double quotes for emcc robustness
    out+="\"${item}\""
  done
  out+=']'
  echo "$out"
}

EXPORTED_FUNCTIONS_LIST=$(to_ems_list "${exported_functions[@]}")
DEFAULT_LIBS_LIST=$(to_ems_list "${default_lib_funcs[@]}")
RUNTIME_METHODS_LIST=$(to_ems_list "${runtime_methods[@]}")

object_files=(m68kcpu.o m68kops.o myfunc.o m68k_memory_bridge.o m68ktrace.o m68kdasm.o)
if [[ "$ENABLE_PERFETTO_FLAG" == "1" ]]; then
  object_files+=(m68k_perfetto.o third_party/retrobus-perfetto/cpp/proto/perfetto.pb.o)
fi

SOURCE_MAP_BASE="${SOURCE_MAP_BASE:-http://localhost:8080/}"

emcc_opts=(
  -g3 -gsource-map --source-map-base "$SOURCE_MAP_BASE"
  "${object_files[@]}"
  -s "EXPORTED_FUNCTIONS=${EXPORTED_FUNCTIONS_LIST}"
  -s "DEFAULT_LIBRARY_FUNCS_TO_INCLUDE=${DEFAULT_LIBS_LIST}"
  -s "EXPORTED_RUNTIME_METHODS=${RUNTIME_METHODS_LIST}"
  -s ASSERTIONS=2
  -s ALLOW_MEMORY_GROWTH=1
  -s ALLOW_TABLE_GROWTH=1
  -s MODULARIZE=1
  -s EXPORT_ES6=1
  -s WASM_ASYNC_COMPILATION=1
  -s WASM=1
  -s DISABLE_EXCEPTION_CATCHING=0
  -s WASM_BIGINT=1
  -Wl,--gc-sections
)

if [[ "$ENABLE_PERFETTO_FLAG" == "1" ]]; then
  echo "==== PERFETTO LINKING SETUP ===="
  export PKG_CONFIG_PATH="third_party/protobuf-wasm-install/lib/pkgconfig:third_party/abseil-wasm-install/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
  echo "PKG_CONFIG_PATH: $PKG_CONFIG_PATH"
  if ! pkg-config --exists protobuf; then
    die "protobuf.pc not found or invalid. Run ./build_protobuf_wasm.sh"
  fi
  # Collect protobuf libs (split on spaces)
  read -r -a protobuf_libs <<< "$(pkg-config --static --libs protobuf)"
  [[ ${#protobuf_libs[@]} -gt 0 ]] || die "No protobuf libs found via pkg-config"
  echo "protobuf libs: ${protobuf_libs[*]}"
  emcc_opts+=("${protobuf_libs[@]}")
fi

echo "==== BUILDING NODE.JS VERSION (ESM) ===="
[[ -f post.js ]] || die "post.js not found next to the build script"

run emcc \
  "${emcc_opts[@]}" \
  -s ENVIRONMENT=node \
  -s EXPORT_NAME=createMusashi \
  --post-js post.js \
  -o musashi-node.out.mjs

echo "==== BUILDING WEB VERSION (ESM) ===="
run emcc \
  "${emcc_opts[@]}" \
  -s ENVIRONMENT=web \
  -s EXPORT_NAME=createMusashi \
  --post-js post.js \
  -o musashi.out.mjs

echo "==== BUILDING UNIVERSAL VERSION (ESM) ===="
run emcc \
  "${emcc_opts[@]}" \
  -s ENVIRONMENT=web,webview,worker,node \
  -s EXPORT_NAME=createMusashi \
  --post-js post.js \
  -o musashi-universal.out.mjs

echo "Build complete:"
ls -lh *.out.mjs *.out.wasm || true
