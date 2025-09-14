/* ======================================================================== */
/* ========================= M68K TRACING FRAMEWORK ====================== */
/* ======================================================================== */

#include "m68ktrace.h"
#include "m68k.h"
#include "m68kcpu.h"
#include <cstring>
#include <cstdint>
#include <climits>
#include <vector>
#include <algorithm>
#include <optional>
#include <functional>
#include <array>

#ifndef UINT64_MAX
#define UINT64_MAX ((uint64_t)-1)
#endif

/* ======================================================================== */
/* ========================== INTERNAL STRUCTURES ======================== */
/* ======================================================================== */

struct m68k_trace_state {
    /* Global enable flag */
    bool enabled = false;
    
    /* Per-feature enable flags */
    bool flow_enabled = false;
    bool mem_enabled = false;
    bool instr_enabled = false;
    
    /* Raw C function pointers for callbacks (preserve exact ABI/signatures).
     * Using raw pointers instead of std::function ensures Emscripten
     * generates the correct wasm table signature, including i64 (BigInt)
     * for the cycles parameter when WASM_BIGINT is enabled. */
    m68k_trace_flow_callback  flow_callback = nullptr;
    m68k_trace_mem_callback   mem_callback = nullptr;
    m68k_trace_instr_callback instr_callback = nullptr;
    
    /* Memory regions to trace - no arbitrary limit */
    std::vector<m68k_trace_region> mem_regions;
    
    /* Cycle counter - raw cycles are critical */
    uint64_t total_cycles = 0;
};

/* Global trace state */
static m68k_trace_state g_trace;

/* ======================================================================== */
/* ========================== INTERNAL FUNCTIONS ========================= */
/* ======================================================================== */

/* Check if an address falls within traced memory regions */
static bool is_address_traced(uint32_t address) noexcept
{
    if (g_trace.mem_regions.empty()) {
        /* If no regions specified, trace all memory */
        return true;
    }
    
    /* Use STL algorithm instead of manual loop */
    return std::any_of(g_trace.mem_regions.begin(), g_trace.mem_regions.end(),
        [address](const auto& region) {
            return address >= region.start && address < region.end;
        });
}

/* ======================================================================== */
/* ============================= PUBLIC API ============================== */
/* ======================================================================== */

extern "C" {

void m68k_trace_enable(int enable)
{
    g_trace.enabled = enable != 0;
}

int m68k_trace_is_enabled(void)
{
    return g_trace.enabled ? 1 : 0;
}

void m68k_set_trace_flow_callback(m68k_trace_flow_callback callback)
{
    g_trace.flow_callback = callback;
}

void m68k_set_trace_mem_callback(m68k_trace_mem_callback callback)
{
    g_trace.mem_callback = callback;
}

void m68k_set_trace_instr_callback(m68k_trace_instr_callback callback)
{
    g_trace.instr_callback = callback;
}

int m68k_trace_add_mem_region(uint32_t start, uint32_t end)
{
    /* Validate parameters */
    if (start >= end) {
        /* Zero-size or negative-size region - reject */
        return -1;
    }
    
    /* Check for duplicate regions */
    auto it = std::find_if(g_trace.mem_regions.begin(), g_trace.mem_regions.end(),
        [start, end](const auto& region) {
            return region.start == start && region.end == end;
        });
    
    if (it != g_trace.mem_regions.end()) {
        /* Duplicate region - silently ignore */
        return 0;
    }
    
    /* Add new region - no arbitrary limit */
    g_trace.mem_regions.push_back({start, end});
    return 0;
}

void m68k_trace_clear_mem_regions(void)
{
    g_trace.mem_regions.clear();
}

void m68k_trace_set_flow_enabled(int enable)
{
    g_trace.flow_enabled = enable != 0;
}

void m68k_trace_set_mem_enabled(int enable)
{
    g_trace.mem_enabled = enable != 0;
}

void m68k_trace_set_instr_enabled(int enable)
{
    g_trace.instr_enabled = enable != 0;
}

uint64_t m68k_get_total_cycles(void)
{
    return g_trace.total_cycles;
}

void m68k_reset_total_cycles(void)
{
    g_trace.total_cycles = 0;
}

/* ======================================================================== */
/* ==================== INTERNAL HOOK FUNCTIONS ========================== */
/* ======================================================================== */

/* These functions are called from the CPU core */

/* Called before each instruction execution */
int m68k_trace_instruction_hook(unsigned int pc, uint16_t opcode, int cycles_executed)
{
    int result = 0;
    
    /* Check all conditions before calling callback */
    if (g_trace.enabled && g_trace.instr_enabled && g_trace.instr_callback) {
        /* Call callback with protection against exceptions */
        result = g_trace.instr_callback(pc, opcode, g_trace.total_cycles, cycles_executed);
        
        /* Sanitize return value */
        if (result < 0) result = 0;
    }
    
    return result;
}

/* Called for control flow changes */
int m68k_trace_flow_hook(m68k_trace_flow_type type, uint32_t source_pc, 
                         uint32_t dest_pc, uint32_t return_addr)
{
    int result = 0;
    
    /* Validate parameters */
    if (type < M68K_TRACE_FLOW_CALL || type > M68K_TRACE_FLOW_EXCEPTION) {
        return 0;
    }
    
    
    if (g_trace.enabled && g_trace.flow_enabled && g_trace.flow_callback) {
        /* Get current register state with bounds checking */
        std::array<uint32_t, 8> d_regs;
        std::array<uint32_t, 8> a_regs;
        
        for (int i = 0; i < 8; i++) {
            d_regs[i] = m68k_get_reg(nullptr, static_cast<m68k_register_t>(M68K_REG_D0 + i));
            a_regs[i] = m68k_get_reg(nullptr, static_cast<m68k_register_t>(M68K_REG_A0 + i));
        }
        
        /* Call callback with protection */
        result = g_trace.flow_callback(type, source_pc, dest_pc, return_addr,
                                       d_regs.data(), a_regs.data(), g_trace.total_cycles);
        
        /* Sanitize return value */
        if (result < 0) result = 0;
    }
    
    return result;
}

/* Called for memory accesses */
int m68k_trace_mem_hook(m68k_trace_mem_type type, uint32_t pc,
                       uint32_t address, uint32_t value, uint8_t size)
{
    int result = 0;
    
    /* Validate parameters */
    if (type != M68K_TRACE_MEM_READ && type != M68K_TRACE_MEM_WRITE) {
        return 0;
    }
    
    if (size != 1 && size != 2 && size != 4) {
        return 0; /* Invalid size */
    }
    
    if (g_trace.enabled && g_trace.mem_enabled && g_trace.mem_callback) {
        if (is_address_traced(address)) {
            /* Call callback with protection */
            result = g_trace.mem_callback(type, pc, address, value, size,
                                          g_trace.total_cycles);
            
            /* Sanitize return value */
            if (result < 0) result = 0;
        }
    }
    
    return result;
}

/* Update cycle counter - called from CPU core after each instruction */
void m68k_trace_update_cycles(int cycles_executed)
{
    if (g_trace.enabled && cycles_executed > 0) {
        /* Check for potential overflow */
        uint64_t new_total = g_trace.total_cycles + static_cast<uint64_t>(cycles_executed);
        
        /* Handle overflow by capping at max value */
        if (new_total < g_trace.total_cycles) {
            g_trace.total_cycles = UINT64_MAX;
        } else {
            g_trace.total_cycles = new_total;
        }
    }
}

} // extern "C"
