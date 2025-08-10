#!/usr/bin/env fish

# Note: Perfetto tracing can be enabled by setting ENABLE_PERFETTO=1 environment variable
# Requires running ./build_protobuf_wasm.sh first to build protobuf and abseil dependencies

# === Fail-fast helpers (Fish doesn't have "set -e") ===
function die
    set -l code $status
    if test (count $argv) -gt 0
        echo (string join ' ' $argv) >&2
    end
    exit (test $code -ne 0; and echo $code; or echo 1)
end

function run
    echo
    echo ">>> $argv"
    command $argv
    or die "FAILED ($status): $argv"
end

# === Environment sanity: show the toolchain we actually see in this shell ===
echo "==== TOOLCHAIN VERIFICATION ===="

# Fish may not inherit PATH properly from bash - ensure Emscripten tools are in PATH
if test -n "$EMSDK"
    echo "Found EMSDK: $EMSDK"
    set -gx PATH $EMSDK/upstream/emscripten $EMSDK $PATH
else
    echo "EMSDK not set, trying to find Emscripten..."
    # Look for emcc in common locations
    for emcc_path in /home/runner/work/_temp/*/emsdk-main/upstream/emscripten
        if test -f "$emcc_path/emcc"
            echo "Found emcc at: $emcc_path"
            set -gx PATH $emcc_path (dirname $emcc_path) $PATH
            set -gx EMSDK (dirname (dirname $emcc_path))
            break
        end
    end
end

echo "Current PATH: $PATH"

# Test if we can find emcc directly before using which
if test -x "$EMSDK/upstream/emscripten/emcc"
    echo "emcc found at: $EMSDK/upstream/emscripten/emcc"
else
    die "emcc not found at expected location: $EMSDK/upstream/emscripten/emcc"
end

# Now try the which commands (these should work after PATH is set)
echo "Checking which commands..."
which emcc     ^/dev/null; or echo "WARNING: which emcc failed, but direct path works"
which em++     ^/dev/null; or echo "WARNING: which em++ failed, but should work via PATH" 
which emmake   ^/dev/null; or echo "WARNING: which emmake failed, but should work via PATH"
echo "emcc version:"
emcc --version
echo "node version:"
node --version
echo "================================"

# Check if we should enable Perfetto
set -l enable_perfetto (set -q ENABLE_PERFETTO; and echo $ENABLE_PERFETTO; or echo "0")

if test "$enable_perfetto" = "1"
    echo "Building with Perfetto tracing enabled..."
    set -x ENABLE_PERFETTO 1
else
    echo "Building without Perfetto tracing (set ENABLE_PERFETTO=1 to enable)..."
end

# Build with correct flags (limit parallelism for CI stability)
run emmake make -j8 ENABLE_PERFETTO=$enable_perfetto

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
    _m68k_reset_total_cycles \
    _m68k_disassemble \
    _m68k_perfetto_init \
    _m68k_perfetto_destroy \
    _m68k_perfetto_enable_flow \
    _m68k_perfetto_enable_memory \
    _m68k_perfetto_enable_instructions \
    _m68k_perfetto_export_trace \
    _m68k_perfetto_free_trace_data \
    _m68k_perfetto_save_trace \
    _m68k_perfetto_is_initialized \
    _m68k_perfetto_cleanup_slices

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

set -l object_files m68kcpu.o m68kops.o myfunc.o m68ktrace.o m68kdasm.o

# Add Perfetto-related files and libraries if enabled
if test "$enable_perfetto" = "1"
    set object_files $object_files m68k_perfetto.o third_party/retrobus-perfetto/cpp/proto/perfetto.pb.o
end

set -l emcc_options \
 -g3 \
 -gsource-map \
 --source-map-base http://localhost:8080/ \
 $object_files \
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
 # Enable JS exceptions for Perfetto support (WASM exceptions cause linker crash)
 -sDISABLE_EXCEPTION_CATCHING=0 \
 -Wl,--gc-sections

# Add protobuf and abseil libraries if Perfetto is enabled  
if test "$enable_perfetto" = "1"
    echo "==== PERFETTO LINKING SETUP ===="
    
    # Fish treats *_PATH variables as lists, so set as a list (exported as colon-separated)
    set -gx PKG_CONFIG_PATH third_party/protobuf-wasm-install/lib/pkgconfig \
                            third_party/abseil-wasm-install/lib/pkgconfig
    
    echo "PKG_CONFIG_PATH: $PKG_CONFIG_PATH"
    
    # Verify protobuf pkg-config works
    echo "Testing pkg-config for protobuf..."
    pkg-config --exists protobuf; or die "protobuf.pc not found or invalid"
    
    # Get protobuf linking flags and split them properly (CRITICAL for Fish)
    echo "Getting protobuf link flags..."
    set -l protobuf_libs (pkg-config --static --libs protobuf | string split -n ' ')
    
    echo "protobuf libs (split):"
    for lib in $protobuf_libs
        echo "  $lib"
    end
    
    # Add the properly split protobuf libraries to emcc options
    set emcc_options $emcc_options $protobuf_libs
    
    echo "================================="
end

# Build Node.js version
echo "==== BUILDING NODE.JS VERSION ===="
run emcc \
 $emcc_options \
 -s ENVIRONMENT=node \
 -s EXPORT_NAME=createMusashi \
 --post-js post.js \
 -o musashi-node.out.js
echo "Written to musashi-node.out.js"

# Build Web version  
echo "==== BUILDING WEB VERSION ===="
run emcc \
 $emcc_options \
 -s ENVIRONMENT=web \
 -s EXPORT_NAME=createMusashi \
 --post-js post.js \
 -o musashi.out.js
echo "Written to musashi.out.js"