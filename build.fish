#!/usr/bin/env fish

emmake make -j20

set -l exported_functions \
    _malloc \
    _free \
    _m68k_set_reg \
    _m68k_get_reg \
    _m68k_init \
    _m68k_execute \
    _m68k_set_context \
    _set_read_mem_func \
    _set_write_mem_func \
    _set_pc_hook_func \
    _add_pc_hook_addr \
    _add_region \
    _clear_regions \
    _m68k_pulse_reset \
    _m68k_cycles_run \
    _enable_printf_logging \
    _my_initialize \
    _m68k_trace_enable \
    _m68k_trace_is_enabled \
    _m68k_set_trace_flow_callback \
    _m68k_set_trace_mem_callback \
    _m68k_set_trace_instr_callback \
    _m68k_trace_add_mem_region \
    _m68k_trace_clear_mem_regions \
    _m68k_trace_set_flow_enabled \
    _m68k_trace_set_mem_enabled \
    _m68k_trace_set_instr_enabled \
    _m68k_get_total_cycles \
    _m68k_reset_total_cycles

set -l runtime_methods \
    addFunction \
    ccall \
    cwrap \
    getValue \
    removeFunction \
    setValue \
    writeArrayToMemory \
    UTF8ToString \
    stringToUTF8 \
    stackTrace

set -l emcc_options \
 -g3 \
 -gsource-map \
 --source-map-base http://localhost:8080/ \
 m68kcpu.o m68kops.o myfunc.o m68ktrace.o \
 -s EXPORTED_FUNCTIONS=(string join ',' $exported_functions) \
 -s EXPORTED_RUNTIME_METHODS=(string join ',' $runtime_methods) \
 # https://emscripten.org/docs/porting/guidelines/function_pointer_issues.html
#  -s SAFE_HEAP \
 -s ASSERTIONS=2 \
#  -s RESERVED_FUNCTION_POINTERS=256 \
 # default is 16MB, ALLOW_MEMORY_GROWTH is alternative.
#  -s INITIAL_MEMORY=16MB \
 -s ALLOW_MEMORY_GROWTH \
 # https://github.com/emscripten-core/emscripten/issues/7082#issuecomment-462957723
#  -s EMULATE_FUNCTION_POINTER_CASTS \
 # https://github.com/emscripten-core/emscripten/issues/7082#issuecomment-462957723
 -s ALLOW_TABLE_GROWTH=1 \
#  -s USE_OFFSET_CONVERTER=1 \
 # without this it didn't find the .wasm file?
 -s MODULARIZE=1 \
 # async is important, or we won't be able to download .wasm easily in observablehq.com
 -s WASM_ASYNC_COMPILATION=1 \
 # having separate .wasm allows for a source map to work
 # also required for WASM_BIGINT
 -s WASM=1 \
#  -fsanitize=address \
 # important since we want to operate on 32 and 64-bit numbers
 -sWASM_BIGINT \
 -flto

emcc \
 $emcc_options \
 -s ENVIRONMENT=node \
 -o musashi-node.out.js
# FIXME: add as prefix: // @ts-nocheck
cat post.js >> musashi-node.out.js
echo "Written to musashi-node.out.js"

emcc \
 $emcc_options \
 -s ENVIRONMENT=web \
 -o musashi.out.js
cat post.js >> musashi.out.js
echo "Written to musashi.out.js"