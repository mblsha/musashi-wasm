/* ======================================================================== */
/* ======================= M68K PERFETTO INTEGRATION ==================== */
/* ======================================================================== */

#ifdef ENABLE_PERFETTO

#include "m68k_perfetto.h"
#include "m68k.h"
#include <retrobus/retrobus_perfetto.hpp>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <cassert>

/* ======================================================================== */
/* ========================== GLOBAL STATE =============================== */
/* ======================================================================== */

static std::unique_ptr<m68k_perfetto::M68kPerfettoTracer> g_tracer;

/* External symbol name lookups from myfunc.cc */
extern "C" {
    const char* get_function_name(unsigned int address);
    const char* get_memory_name(unsigned int address);
}

/* ======================================================================== */
/* ======================= CALLBACK IMPLEMENTATIONS ====================== */
/* ======================================================================== */

/* C callback wrappers that delegate to the C++ tracer */
extern "C" {
    static int perfetto_flow_callback(m68k_trace_flow_type type, uint32_t source_pc, 
                                     uint32_t dest_pc, uint32_t return_addr,
                                     const uint32_t* d_regs, const uint32_t* a_regs, 
                                     uint64_t cycles) {
        if (g_tracer) {
            return g_tracer->handle_flow_event(type, source_pc, dest_pc, return_addr,
                                              d_regs, a_regs, cycles);
        }
        return 0;
    }

    static int perfetto_memory_callback(m68k_trace_mem_type type, uint32_t pc,
                                       uint32_t address, uint32_t value, uint8_t size, 
                                       uint64_t cycles) {
        if (g_tracer) {
            return g_tracer->handle_memory_event(type, pc, address, value, size, cycles);
        }
        return 0;
    }

    static int perfetto_instruction_callback(uint32_t pc, uint16_t opcode, uint64_t start_cycles, int instr_cycles) {
        if (g_tracer) {
            return g_tracer->handle_instruction_event(pc, opcode, start_cycles, instr_cycles);
        }
        return 0;
    }
}

/* ======================================================================== */
/* ======================= PUBLIC C API IMPLEMENTATION =================== */
/* ======================================================================== */

extern "C" {

int m68k_perfetto_init(const char* process_name) {
    if (g_tracer) {
        return -1; /* Already initialized */
    }

    try {
        g_tracer = std::make_unique<m68k_perfetto::M68kPerfettoTracer>(
            process_name ? process_name : "M68K_Emulator");
        
        /* Register callbacks with m68ktrace system */
        m68k_set_trace_flow_callback(perfetto_flow_callback);
        m68k_set_trace_mem_callback(perfetto_memory_callback);
        m68k_set_trace_instr_callback(perfetto_instruction_callback);
        
        return 0;
    } catch (const std::exception&) {
        g_tracer.reset();
        return -1;
    }
}

void m68k_perfetto_cleanup_slices(void) {
    if (g_tracer) {
        g_tracer->cleanup_unclosed_slices();
    }
}

void m68k_perfetto_destroy(void) {
    if (g_tracer) {
        /* Unregister callbacks */
        m68k_set_trace_flow_callback(nullptr);
        m68k_set_trace_mem_callback(nullptr);
        m68k_set_trace_instr_callback(nullptr);
        
        g_tracer.reset();
    }
}

void m68k_perfetto_enable_flow(int enable) {
    if (g_tracer) {
        g_tracer->enable_flow_tracing(enable != 0);
        m68k_trace_set_flow_enabled(enable);
    }
}

void m68k_perfetto_enable_memory(int enable) {
    if (g_tracer) {
        g_tracer->enable_memory_tracing(enable != 0);
        m68k_trace_set_mem_enabled(enable);
    }
}

void m68k_perfetto_enable_instructions(int enable) {
    if (g_tracer) {
        g_tracer->enable_instruction_tracing(enable != 0);
        m68k_trace_set_instr_enabled(enable);
    }
}

int m68k_perfetto_export_trace(uint8_t** data_out, size_t* size_out) {
    if (!g_tracer || !data_out || !size_out) {
        return -1;
    }

    try {
        auto trace_data = g_tracer->serialize();
        if (trace_data.empty()) {
            *data_out = nullptr;
            *size_out = 0;
            return 0;
        }

        /* Allocate memory that can be freed with standard free() */
        uint8_t* data = static_cast<uint8_t*>(malloc(trace_data.size()));
        if (!data) {
            return -1;
        }

        std::copy(trace_data.begin(), trace_data.end(), data);
        *data_out = data;
        *size_out = trace_data.size();
        return 0;
    } catch (const std::exception&) {
        return -1;
    }
}

void m68k_perfetto_free_trace_data(uint8_t* data) {
    free(data);
}

int m68k_perfetto_save_trace(const char* filename) {
    if (!g_tracer || !filename) {
        return -1;
    }

    try {
        g_tracer->save_to_file(filename);
        return 0;
    } catch (const std::exception&) {
        return -1;
    }
}

int m68k_perfetto_is_initialized(void) {
    return g_tracer ? 1 : 0;
}

} /* extern "C" */

/* ======================================================================== */
/* ===================== M68K PERFETTO TRACER IMPLEMENTATION ============= */
/* ======================================================================== */

namespace m68k_perfetto {

/* CPU frequency for timing calculations (assume 8MHz M68000) */
constexpr uint64_t CPU_FREQ_HZ = 8000000;

M68kPerfettoTracer::M68kPerfettoTracer(const std::string& process_name) 
    : flow_enabled_(false)
    , memory_enabled_(false)
    , instruction_enabled_(false)
    , total_instructions_(0)
    , total_memory_accesses_(0) {
    
    trace_builder_ = std::make_unique<retrobus::PerfettoTraceBuilder>(process_name);
    
    /* Create tracks for different event types */
    cpu_thread_track_id_ = trace_builder_->add_thread("Flow");
    jumps_thread_track_id_ = trace_builder_->add_thread("Jumps");
    instr_thread_track_id_ = trace_builder_->add_thread("Instructions");
    memory_writes_track_id_ = trace_builder_->add_thread("Writes");
    memory_counter_track_id_ = trace_builder_->add_counter_track("Memory_Access", "count");
    cycle_counter_track_id_ = trace_builder_->add_counter_track("CPU_Cycles", "cycles");
}

M68kPerfettoTracer::~M68kPerfettoTracer() {
    /* Auto-cleanup on destruction */
    cleanup_unclosed_slices();
}

void M68kPerfettoTracer::cleanup_unclosed_slices() {
    /* Close any remaining open slices to prevent "Did not end" in Perfetto UI */
    if (!call_stack_.empty()) {
        /* Get a reasonable end timestamp (current cycles + small offset) */
        uint64_t cleanup_timestamp_ns = cycles_to_nanoseconds(999999);  /* ~125ms at 8MHz */
        
        /* Close all remaining slices in reverse order (LIFO) */
        while (!call_stack_.empty()) {
            trace_builder_->end_slice(cpu_thread_track_id_, cleanup_timestamp_ns);
            call_stack_.pop_back();
            cleanup_timestamp_ns += 1000;  /* Small time increment between closes */
        }
    }
}

int M68kPerfettoTracer::handle_flow_event(m68k_trace_flow_type type, uint32_t source_pc, 
                                         uint32_t dest_pc, uint32_t return_addr,
                                         const uint32_t* d_regs, const uint32_t* a_regs, 
                                         uint64_t cycles) {
    if (!flow_enabled_) {
        return 0;
    }

    uint64_t timestamp_ns = cycles_to_nanoseconds(cycles);

    switch (type) {
        case M68K_TRACE_FLOW_CALL: {
            /* Begin a slice for the call and track it on call stack */
            /* Use registered function name if available */
            const char* func_name = get_function_name(dest_pc);
            auto call_name = func_name ? std::string(func_name) : (std::string("call_") + format_hex(dest_pc));
            
            auto event = trace_builder_->begin_slice(cpu_thread_track_id_, call_name, timestamp_ns)
                .add_pointer("source_pc", source_pc)
                .add_pointer("target_pc", dest_pc)
                .add_pointer("return_addr", return_addr);
            
            /* Add function name as annotation if available */
            if (func_name) {
                event.add_annotation("func_name", func_name);
            }
            
            /* Add registers as a nested dictionary under "r" */
            if (d_regs && a_regs) {
                event.annotation("r")
                    .pointer("d0", d_regs[0])
                    .pointer("d1", d_regs[1])
                    .pointer("d2", d_regs[2])
                    .pointer("d3", d_regs[3])
                    .pointer("d4", d_regs[4])
                    .pointer("d5", d_regs[5])
                    .pointer("d6", d_regs[6])
                    .pointer("d7", d_regs[7])
                    .pointer("a0", a_regs[0])
                    .pointer("a1", a_regs[1])
                    .pointer("a2", a_regs[2])
                    .pointer("a3", a_regs[3])
                    .pointer("a4", a_regs[4])
                    .pointer("a5", a_regs[5])
                    .pointer("a6", a_regs[6])
                    .pointer("a7_sp", a_regs[7]);
            }

            /* Track call for matching with return */
            FlowState flow_state;
            flow_state.slice_id = 0; /* Not needed for this API */
            flow_state.source_pc = source_pc;
            flow_state.flow_name = std::string("call_") + format_hex(dest_pc);
            call_stack_.push_back(flow_state);
            break;
        }

        case M68K_TRACE_FLOW_RETURN: {
            /* End the most recent call slice */
            if (!call_stack_.empty()) {
                auto& flow_state = call_stack_.back();
                
                /* End the slice with proper timestamp */
                trace_builder_->end_slice(cpu_thread_track_id_, timestamp_ns);
                call_stack_.pop_back();
            } else {
                /* Return without matching call - can happen when starting mid-execution */
            }
            break;
        }

        case M68K_TRACE_FLOW_BRANCH_TAKEN:
        case M68K_TRACE_FLOW_JUMP: {
            /* Only trace taken jumps/branches on dedicated Jumps thread */
            /* All taken jumps are shown as "jump" for simplicity */
            trace_builder_->add_instant_event(jumps_thread_track_id_, "jump", timestamp_ns)
                .add_pointer("from", source_pc)
                .add_pointer("to", dest_pc)
                .add_annotation("offset", static_cast<int64_t>(static_cast<int32_t>(dest_pc - source_pc)));
            break;
        }
        
        case M68K_TRACE_FLOW_BRANCH_NOT_TAKEN:
            /* Ignore not-taken branches as requested */
            break;

        case M68K_TRACE_FLOW_EXCEPTION: {
            /* Exception event on Jumps thread */
            trace_builder_->add_instant_event(jumps_thread_track_id_, "exception", timestamp_ns)
                .add_pointer("from", source_pc)
                .add_pointer("to", dest_pc)
                .add_annotation("condition", "exception")
                .add_pointer("vector_addr", dest_pc);
            break;
        }

        case M68K_TRACE_FLOW_TRAP: {
            /* TRAP instruction event on Jumps thread */
            trace_builder_->add_instant_event(jumps_thread_track_id_, "trap", timestamp_ns)
                .add_pointer("from", source_pc)
                .add_pointer("to", dest_pc)
                .add_annotation("condition", "trap")
                .add_pointer("trap_vector", dest_pc);
            break;
        }

        case M68K_TRACE_FLOW_EXCEPTION_RETURN: {
            /* Exception return (RTE) event on Jumps thread */
            trace_builder_->add_instant_event(jumps_thread_track_id_, "exception_return", timestamp_ns)
                .add_pointer("from", source_pc)
                .add_pointer("to", dest_pc)
                .add_annotation("condition", "exception_return");
            break;
        }
    }

    return 0; /* Continue execution */
}

int M68kPerfettoTracer::handle_memory_event(m68k_trace_mem_type type, uint32_t pc,
                                           uint32_t address, uint32_t value, uint8_t size, 
                                           uint64_t cycles) {
    if (!memory_enabled_) {
        return 0;
    }

    uint64_t timestamp_ns = cycles_to_nanoseconds(cycles);
    total_memory_accesses_++;

    /* Create slice for memory writes on dedicated Writes thread */
    if (type == M68K_TRACE_MEM_WRITE) {
        std::string write_name = std::string("write_") + std::to_string(size) + "B";
        
        /* Create instant slice for memory write */
        auto mem_event = trace_builder_->add_instant_event(memory_writes_track_id_, write_name, timestamp_ns)
            .add_pointer("pc", pc)
            .add_pointer("address", address)
            .add_pointer("value", value)
            .add_annotation("size", static_cast<int64_t>(size));
        
        /* Add memory name annotation if available */
        const char* mem_name = get_memory_name(address);
        if (mem_name) {
            mem_event.add_annotation("name", mem_name);
        }
    }

    /* Update counter track */
    trace_builder_->update_counter(memory_counter_track_id_, static_cast<double>(total_memory_accesses_), timestamp_ns);

    return 0; /* Continue execution */
}

int M68kPerfettoTracer::handle_instruction_event(uint32_t pc, uint16_t opcode, uint64_t start_cycles, int instr_cycles) {
    if (!instruction_enabled_) {
        return 0;
    }

    uint64_t start_ns = cycles_to_nanoseconds(start_cycles);
    uint64_t end_ns = cycles_to_nanoseconds(start_cycles + instr_cycles);
    
    /* Ensure duration is at least 1ns to be visible */
    if (start_ns == end_ns) {
        end_ns++;
    }

    total_instructions_++;

    /* Get disassembled instruction for better readability */
    char disasm_buf[100];
    m68k_disassemble(disasm_buf, pc, M68K_CPU_TYPE_68000);

    /* Create slice for instruction execution on its own track */
    trace_builder_->begin_slice(instr_thread_track_id_, disasm_buf, start_ns)
        .add_pointer("pc", pc);

    trace_builder_->end_slice(instr_thread_track_id_, end_ns);

    /* Update cycle counter on the main track */
    trace_builder_->update_counter(cycle_counter_track_id_, static_cast<double>(start_cycles), start_ns);

    return 0; /* Continue execution */
}

std::vector<uint8_t> M68kPerfettoTracer::serialize() const {
    return trace_builder_->serialize();
}

void M68kPerfettoTracer::save_to_file(const std::string& filename) const {
    trace_builder_->save(filename);
}

std::string M68kPerfettoTracer::format_hex(uint32_t value) const {
    std::ostringstream oss;
    oss << "0x" << std::hex << std::setfill('0') << std::setw(8) << value;
    return oss.str();
}

/* format_registers is no longer needed - using nested dictionary annotations instead */

uint64_t M68kPerfettoTracer::cycles_to_nanoseconds(uint64_t cycles) const {
    /* Convert CPU cycles to nanoseconds based on CPU frequency */
    return (cycles * 1000000000ULL) / CPU_FREQ_HZ;
}

} /* namespace m68k_perfetto */

#endif /* ENABLE_PERFETTO */