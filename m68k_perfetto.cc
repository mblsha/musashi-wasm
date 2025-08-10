/* ======================================================================== */
/* ======================= M68K PERFETTO INTEGRATION ==================== */
/* ======================================================================== */

#ifdef ENABLE_PERFETTO

#include "m68k_perfetto.h"
#include <retrobus/retrobus_perfetto.hpp>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <cassert>

/* ======================================================================== */
/* ========================== GLOBAL STATE =============================== */
/* ======================================================================== */

static std::unique_ptr<m68k_perfetto::M68kPerfettoTracer> g_tracer;

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

    static int perfetto_instruction_callback(uint32_t pc, uint16_t opcode, uint64_t cycles) {
        if (g_tracer) {
            return g_tracer->handle_instruction_event(pc, opcode, cycles);
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
    cpu_thread_track_id_ = trace_builder_->add_thread("M68K_CPU");
    memory_counter_track_id_ = trace_builder_->add_counter_track("Memory_Access", "count");
    cycle_counter_track_id_ = trace_builder_->add_counter_track("CPU_Cycles", "cycles");
}

M68kPerfettoTracer::~M68kPerfettoTracer() = default;

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
            auto call_name = std::string("call_") + format_hex(dest_pc);
            auto event = trace_builder_->begin_slice(cpu_thread_track_id_, call_name, timestamp_ns)
                .add_annotation("source_pc", format_hex(source_pc))
                .add_annotation("target_pc", format_hex(dest_pc))
                .add_annotation("return_addr", format_hex(return_addr))
                .add_annotation("d0", static_cast<int64_t>(d_regs[0]))
                .add_annotation("a7_sp", format_hex(a_regs[7]));

            if (d_regs && a_regs) {
                event.add_annotation("registers", format_registers(d_regs, a_regs));
            }

            /* Track call for matching with return */
            FlowState flow_state;
            flow_state.slice_id = 0; /* Not needed for this API */
            flow_state.source_pc = source_pc;
            flow_state.flow_name = std::string("flow_") + format_hex(source_pc) + "_to_" + format_hex(dest_pc);
            call_stack_.push_back(flow_state);

            /* Begin flow event */
            uint64_t flow_id = source_pc; /* Use source PC as flow ID */
            auto flow_event = trace_builder_->add_flow(cpu_thread_track_id_, flow_state.flow_name, timestamp_ns, flow_id, false);
            (void)flow_event; /* Suppress unused warning */
            break;
        }

        case M68K_TRACE_FLOW_RETURN: {
            /* End the most recent call slice */
            if (!call_stack_.empty()) {
                auto& flow_state = call_stack_.back();
                
                /* Add return event with annotations */
                trace_builder_->add_instant_event(cpu_thread_track_id_, "return", timestamp_ns)
                    .add_annotation("return_pc", format_hex(source_pc))
                    .add_annotation("d0_result", static_cast<int64_t>(d_regs ? d_regs[0] : 0));
                
                /* End the slice */
                trace_builder_->end_slice(cpu_thread_track_id_, timestamp_ns);

                /* End flow event */
                uint64_t flow_id = flow_state.source_pc;
                auto end_flow_event = trace_builder_->add_flow(cpu_thread_track_id_, flow_state.flow_name, timestamp_ns, flow_id, true);
                (void)end_flow_event; /* Suppress unused warning */
                call_stack_.pop_back();
            }
            break;
        }

        case M68K_TRACE_FLOW_BRANCH_TAKEN:
        case M68K_TRACE_FLOW_BRANCH_NOT_TAKEN:
        case M68K_TRACE_FLOW_JUMP: {
            /* Instant event for branches/jumps */
            const char* event_name;
            const char* condition_type;
            if (type == M68K_TRACE_FLOW_BRANCH_TAKEN) {
                event_name = "branch_taken";
                condition_type = "taken";
            } else if (type == M68K_TRACE_FLOW_BRANCH_NOT_TAKEN) {
                event_name = "branch_not_taken"; 
                condition_type = "not_taken";
            } else {
                event_name = "jump";
                condition_type = "unconditional";
            }
            
            trace_builder_->add_instant_event(cpu_thread_track_id_, event_name, timestamp_ns)
                .add_annotation("source_pc", format_hex(source_pc))
                .add_annotation("target_pc", format_hex(dest_pc))
                .add_annotation("condition", condition_type);
            break;
        }

        case M68K_TRACE_FLOW_EXCEPTION: {
            /* Exception event - high priority instant event */
            trace_builder_->add_instant_event(cpu_thread_track_id_, "exception", timestamp_ns)
                .add_annotation("exception_pc", format_hex(source_pc))
                .add_annotation("vector_addr", format_hex(dest_pc))
                .add_annotation("type", "m68k_exception");
            break;
        }

        case M68K_TRACE_FLOW_TRAP: {
            /* TRAP instruction event */
            trace_builder_->add_instant_event(cpu_thread_track_id_, "trap", timestamp_ns)
                .add_annotation("source_pc", format_hex(source_pc))
                .add_annotation("trap_vector", format_hex(dest_pc))
                .add_annotation("type", "trap_instruction");
            break;
        }

        case M68K_TRACE_FLOW_EXCEPTION_RETURN: {
            /* Exception return (RTE) event */
            trace_builder_->add_instant_event(cpu_thread_track_id_, "exception_return", timestamp_ns)
                .add_annotation("return_pc", format_hex(source_pc))
                .add_annotation("target_pc", format_hex(dest_pc))
                .add_annotation("type", "exception_return");
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

    /* Update counter track */
    trace_builder_->update_counter(memory_counter_track_id_, static_cast<double>(total_memory_accesses_), timestamp_ns);

    return 0; /* Continue execution */
}

int M68kPerfettoTracer::handle_instruction_event(uint32_t pc, uint16_t opcode, uint64_t cycles) {
    if (!instruction_enabled_) {
        return 0;
    }

    uint64_t timestamp_ns = cycles_to_nanoseconds(cycles);
    total_instructions_++;

    /* Create slice for instruction execution */
    trace_builder_->begin_slice(cpu_thread_track_id_, "instr", timestamp_ns)
        .add_annotation("pc", format_hex(pc))
        .add_annotation("opcode", format_hex(opcode))
        .add_annotation("instr_count", static_cast<int64_t>(total_instructions_));

    /* End immediately - instructions are typically very short */
    trace_builder_->end_slice(cpu_thread_track_id_, timestamp_ns + 1000); /* 1μs duration */

    /* Update cycle counter */
    trace_builder_->update_counter(cycle_counter_track_id_, static_cast<double>(cycles), timestamp_ns);

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

std::string M68kPerfettoTracer::format_registers(const uint32_t* d_regs, const uint32_t* a_regs) const {
    std::ostringstream oss;
    oss << "D0-D7: ";
    for (int i = 0; i < 8; i++) {
        if (i > 0) oss << ", ";
        oss << format_hex(d_regs[i]);
    }
    oss << " | A0-A7: ";
    for (int i = 0; i < 8; i++) {
        if (i > 0) oss << ", ";
        oss << format_hex(a_regs[i]);
    }
    return oss.str();
}

uint64_t M68kPerfettoTracer::cycles_to_nanoseconds(uint64_t cycles) const {
    /* Convert CPU cycles to nanoseconds based on CPU frequency */
    return (cycles * 1000000000ULL) / CPU_FREQ_HZ;
}

} /* namespace m68k_perfetto */

#endif /* ENABLE_PERFETTO */