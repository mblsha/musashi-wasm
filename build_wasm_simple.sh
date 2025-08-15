#!/bin/bash
set -e

echo "Building with Emscripten 4.x runtime exports fix..."

# Build object files
emmake make -j8

# Core functions that are always exported (removed Perfetto functions that don't exist)
EXPORTED_FUNCTIONS="_malloc,_free,_m68k_set_reg,_m68k_get_reg,_m68k_init,_m68k_execute,_m68k_set_context,_set_read_mem_func,_set_write_mem_func,_set_pc_hook_func,_add_pc_hook_addr,_add_region,_clear_regions,_clear_pc_hook_addrs,_clear_pc_hook_func,_reset_myfunc_state,_register_function_name,_register_memory_name,_register_memory_range,_clear_registered_names,_get_function_name,_get_memory_name,_m68k_pulse_reset,_m68k_end_timeslice,_m68k_cycles_run,_enable_printf_logging,_my_initialize,_m68k_trace_enable,_m68k_trace_is_enabled,_m68k_set_trace_flow_callback,_m68k_set_trace_mem_callback,_m68k_set_trace_instr_callback,_m68k_trace_add_mem_region,_m68k_trace_clear_mem_regions,_m68k_trace_set_flow_enabled,_m68k_trace_set_mem_enabled,_m68k_trace_set_instr_enabled,_m68k_get_total_cycles,_m68k_reset_total_cycles,_m68k_disassemble,_set_read8_callback,_set_write8_callback,_set_probe_callback,_set_d_reg,_get_d_reg,_set_a_reg,_get_a_reg,_set_pc_reg,_get_pc_reg,_set_sr_reg,_get_sr_reg,_set_isp_reg,_set_usp_reg,_get_sp_reg"

# Runtime functions to include (Emscripten 4.x format with '$' prefix)
# Note: $stackTrace is deprecated in Emscripten 4.x
DEFAULT_LIBRARY_FUNCS="\$addFunction,\$removeFunction,\$ccall,\$cwrap,\$getValue,\$setValue,\$UTF8ToString,\$stringToUTF8,\$writeArrayToMemory"

# Heap views and runtime methods still exported via EXPORTED_RUNTIME_METHODS
# Note: In Emscripten 4.x, some functions need to be in both places
EXPORTED_RUNTIME_METHODS="HEAP8,HEAPU8,HEAP16,HEAPU16,HEAP32,HEAPU32,HEAPF32,HEAPF64,addFunction,removeFunction,ccall,cwrap,getValue,setValue,UTF8ToString,stringToUTF8,writeArrayToMemory"

OBJECT_FILES="m68kcpu.o m68kops.o myfunc.o m68k_memory_bridge.o m68ktrace.o m68kdasm.o"

# Common options
EMCC_OPTIONS="-g3 -gsource-map --source-map-base http://localhost:8080/ ${OBJECT_FILES} -s EXPORTED_FUNCTIONS=[${EXPORTED_FUNCTIONS}] -s DEFAULT_LIBRARY_FUNCS_TO_INCLUDE=[${DEFAULT_LIBRARY_FUNCS}] -s EXPORTED_RUNTIME_METHODS=[${EXPORTED_RUNTIME_METHODS}] -s ASSERTIONS=2 -s ALLOW_MEMORY_GROWTH -s ALLOW_TABLE_GROWTH=1 -s MODULARIZE=1 -s WASM_ASYNC_COMPILATION=1 -s WASM=1 -s WASM_BIGINT -s DISABLE_EXCEPTION_CATCHING=0 -Wl,--gc-sections"

# Build Node.js version
echo "Building Node.js version..."
emcc ${EMCC_OPTIONS} -s ENVIRONMENT=node -s EXPORT_NAME=createMusashi --post-js post.js -o musashi-node.out.js
echo "Written to musashi-node.out.js"

# Build Web version
echo "Building Web version..."
emcc ${EMCC_OPTIONS} -s ENVIRONMENT=web -s EXPORT_NAME=createMusashi --post-js post.js -o musashi.out.js
echo "Written to musashi.out.js"

echo "Build complete!"