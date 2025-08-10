/* ======================================================================== */
/* ======================= M68K PERFETTO INTEGRATION ==================== */
/* ======================================================================== */

#ifndef M68K_PERFETTO_H
#define M68K_PERFETTO_H

#include "m68ktrace.h"

/* Only enable if explicitly requested and not in WASM by default */
#ifdef ENABLE_PERFETTO

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/* Forward declaration to avoid including heavy Perfetto headers in public interface */
namespace retrobus {
    class PerfettoTraceBuilder;
}

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================== */
/* ========================== PUBLIC C API =============================== */
/* ======================================================================== */

/* Lifecycle management */
int m68k_perfetto_init(const char* process_name);
void m68k_perfetto_destroy(void);

/* Feature enable/disable */
void m68k_perfetto_enable_flow(int enable);
void m68k_perfetto_enable_memory(int enable);
void m68k_perfetto_enable_instructions(int enable);

/* Export trace data (critical for WASM) */
int m68k_perfetto_export_trace(uint8_t** data_out, size_t* size_out);
void m68k_perfetto_free_trace_data(uint8_t* data);

/* Native-only file save */
int m68k_perfetto_save_trace(const char* filename);

/* Status */
int m68k_perfetto_is_initialized(void);

#ifdef __cplusplus
} /* extern "C" */

/* ======================================================================== */
/* ======================= INTERNAL C++ INTERFACE ======================= */
/* ======================================================================== */

namespace m68k_perfetto {

/* M68K-specific Perfetto backend that implements m68ktrace callbacks */
class M68kPerfettoTracer {
public:
    explicit M68kPerfettoTracer(const std::string& process_name);
    ~M68kPerfettoTracer();

    /* Delete copy/move - this is a singleton-like resource */
    M68kPerfettoTracer(const M68kPerfettoTracer&) = delete;
    M68kPerfettoTracer& operator=(const M68kPerfettoTracer&) = delete;
    M68kPerfettoTracer(M68kPerfettoTracer&&) = delete;
    M68kPerfettoTracer& operator=(M68kPerfettoTracer&&) = delete;

    /* Configuration */
    void enable_flow_tracing(bool enable) { flow_enabled_ = enable; }
    void enable_memory_tracing(bool enable) { memory_enabled_ = enable; }
    void enable_instruction_tracing(bool enable) { instruction_enabled_ = enable; }

    /* Implement m68ktrace callback interfaces */
    int handle_flow_event(m68k_trace_flow_type type, uint32_t source_pc, 
                         uint32_t dest_pc, uint32_t return_addr,
                         const uint32_t* d_regs, const uint32_t* a_regs, 
                         uint64_t cycles);

    int handle_memory_event(m68k_trace_mem_type type, uint32_t pc,
                           uint32_t address, uint32_t value, uint8_t size, 
                           uint64_t cycles);

    int handle_instruction_event(uint32_t pc, uint16_t opcode, uint64_t cycles);

    /* Export functionality */
    std::vector<uint8_t> serialize() const;
    void save_to_file(const std::string& filename) const;

private:
    /* Trace builder and track IDs */
    std::unique_ptr<retrobus::PerfettoTraceBuilder> trace_builder_;
    uint64_t cpu_thread_track_id_;
    uint64_t memory_counter_track_id_;
    uint64_t cycle_counter_track_id_;

    /* Feature flags */
    bool flow_enabled_;
    bool memory_enabled_;
    bool instruction_enabled_;

    /* Internal state for flow tracking */
    struct FlowState {
        uint64_t slice_id;
        uint32_t source_pc;
        std::string flow_name;
    };
    std::vector<FlowState> call_stack_;

    /* Performance counters */
    uint64_t total_instructions_;
    uint64_t total_memory_accesses_;

    /* Utility functions */
    std::string format_hex(uint32_t value) const;
    std::string format_registers(const uint32_t* d_regs, const uint32_t* a_regs) const;
    uint64_t cycles_to_nanoseconds(uint64_t cycles) const;
};

} /* namespace m68k_perfetto */

#endif /* __cplusplus */

#else /* !ENABLE_PERFETTO */

/* ======================================================================== */
/* ===================== NO-OP STUBS WHEN DISABLED ====================== */
/* ======================================================================== */

#ifdef __cplusplus
extern "C" {
#endif

/* Provide no-op implementations when Perfetto is disabled */
static inline int m68k_perfetto_init(const char* process_name) { (void)process_name; return 0; }
static inline void m68k_perfetto_destroy(void) {}
static inline void m68k_perfetto_enable_flow(int enable) { (void)enable; }
static inline void m68k_perfetto_enable_memory(int enable) { (void)enable; }
static inline void m68k_perfetto_enable_instructions(int enable) { (void)enable; }
static inline int m68k_perfetto_export_trace(uint8_t** data_out, size_t* size_out) { 
    (void)data_out; (void)size_out; return -1; 
}
static inline void m68k_perfetto_free_trace_data(uint8_t* data) { (void)data; }
static inline int m68k_perfetto_save_trace(const char* filename) { (void)filename; return -1; }
static inline int m68k_perfetto_is_initialized(void) { return 0; }

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ENABLE_PERFETTO */

#endif /* M68K_PERFETTO_H */