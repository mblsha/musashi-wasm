#!/bin/bash
# Simple build script for Musashi with Perfetto support

set -e

echo "Building Musashi WebAssembly module with Perfetto support..."

# Build with emmake
echo "Building C object files with Perfetto enabled..."
emmake make -j8 ENABLE_PERFETTO=1

# Core functions that are always exported
exported_functions="_malloc,_free,_m68k_init,_m68k_pulse_reset,_m68k_set_cpu_type,_m68k_set_context,_m68k_get_context,_m68k_disassemble,_m68k_is_valid_instruction,_m68k_execute,_m68k_cycles_run,_m68k_cycles_remaining,_m68k_modify_timeslice,_m68k_end_timeslice,_m68k_set_irq,_m68k_set_virq,_m68k_get_reg,_m68k_set_reg,_m68k_pulse_bus_error,_m68k_pulse_halt,_m68k_context_size,_set_read_mem_func,_set_write_mem_func,_set_pc_hook_func,_add_pc_hook_addr,_enable_printf_logging,_add_region,_clear_regions,_reset_myfunc_state"

# Add Perfetto-specific exports
exported_functions="${exported_functions},_m68k_perfetto_init,_m68k_perfetto_destroy,_m68k_perfetto_enable_flow,_m68k_perfetto_enable_memory,_m68k_perfetto_enable_instructions,_m68k_perfetto_export_trace,_m68k_perfetto_free_trace_data,_m68k_perfetto_save_trace,_m68k_perfetto_is_initialized,_m68k_perfetto_cleanup_slices"

# Add symbol naming exports
exported_functions="${exported_functions},_register_function_name,_register_memory_name,_register_memory_range,_clear_registered_names"

# Add wrapper functions
exported_functions="${exported_functions},_perfetto_init,_perfetto_destroy,_perfetto_enable_flow,_perfetto_enable_memory,_perfetto_enable_instructions,_perfetto_export_trace,_perfetto_free_trace_data,_perfetto_save_trace,_perfetto_is_initialized"

# Add tracing functions
exported_functions="${exported_functions},_m68k_trace_enable,_m68k_trace_is_enabled"

# Source files
source_files="myfunc.o m68kcpu.o m68kops.o m68kdasm.o softfloat/softfloat.o m68k_memory_bridge.o m68k_perfetto.o m68ktrace.o third_party/retrobus-perfetto/cpp/proto/perfetto.pb.o"

# Find all required abseil libraries
abseil_libs=""
for lib in third_party/abseil-wasm-install/lib/*.a; do
    if [ -f "$lib" ]; then
        abseil_libs="$abseil_libs $lib"
    fi
done

# Protobuf library
protobuf_lib="third_party/protobuf-wasm-install/lib/libprotobuf.a"

# Common compilation flags
common_flags="-O2 -flto -sEXPORTED_FUNCTIONS=[${exported_functions}] -sEXPORTED_RUNTIME_METHODS=['ccall','getValue'] -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=256MB -sINITIAL_MEMORY=32MB -sWASM_BIGINT=1 -sEXIT_RUNTIME=0 -g"

# Build Node.js version
echo "Building Node.js version..."
emcc ${source_files} ${protobuf_lib} ${abseil_libs} ${common_flags} -sMODULARIZE=1 -sEXPORT_NAME='createMusashiModule' -sENVIRONMENT=node -sRUNTIME_DEBUG=0 -o musashi-node.out.js

echo "Build complete! Files generated:"
echo "  - musashi-node.out.js (Node.js version)"
echo "  - musashi-node.out.wasm"