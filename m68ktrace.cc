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

/* Stop-PC accessors from myfunc.cc (C linkage). Optional for embedders. */
#ifdef MUSASHI_HAVE_STOP_PC_API
extern "C" unsigned int get_stop_pc(void);
extern "C" unsigned int is_stop_pc_enabled(void);
#else
static inline unsigned int get_stop_pc(void) { return 0u; }
static inline unsigned int is_stop_pc_enabled(void) { return 0u; }
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
    
    /* Callbacks using std::function for flexibility */
    std::optional<std::function<int(m68k_trace_flow_type, uint32_t, uint32_t, uint32_t,
                                    const uint32_t*, const uint32_t*, uint64_t)>> flow_callback;
    std::optional<std::function<int(m68k_trace_mem_type, uint32_t, uint32_t, uint32_t,
                                    uint8_t, uint64_t)>> mem_callback;
    std::optional<std::function<int(uint32_t, uint16_t, uint64_t, int)>> instr_callback;
    
    /* Memory regions to trace - no arbitrary limit */
    std::vector<m68k_trace_region> mem_regions;
    
    /* Cycle counter - raw cycles are critical */
    uint64_t total_cycles = 0;
};

/* Global trace state */
static m68k_trace_state g_trace;

/* ======================================================================== */
/* ======================= LIGHTWEIGHT RING BUFFERS ====================== */
/* ======================================================================== */

/* Simple fixed-capacity rings to capture earliest flow/mem events without
 * relying on JS callbacks. This helps debugging in WASM environments. */

struct flow_evt {
    uint32_t type;
    uint32_t src;
    uint32_t dst;
    uint32_t ret;
};

struct mem_evt {
    uint32_t is_read; /* 1 = read, 0 = write */
    uint32_t pc;
    uint32_t addr;
    uint32_t value;
    uint8_t size;
};

static bool flow_ring_enabled = false;
static unsigned flow_ring_limit = 0;
static std::vector<flow_evt> flow_ring;

static bool mem_ring_enabled = false;
static unsigned mem_ring_limit = 0;
static std::vector<mem_evt> mem_ring;

/* First RAM-flow snapshot: capture once when flow enters 0x100000..0x200000 */
static bool first_ram_flow_valid = false;
static flow_evt first_ram_flow_evt{};
static std::array<uint32_t,8> first_ram_d{};
static std::array<uint32_t,8> first_ram_a{};

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
    if (callback) {
        g_trace.flow_callback = callback;
    } else {
        g_trace.flow_callback.reset();
    }
}

void m68k_set_trace_mem_callback(m68k_trace_mem_callback callback)
{
    if (callback) {
        g_trace.mem_callback = callback;
    } else {
        g_trace.mem_callback.reset();
    }
}

void m68k_set_trace_instr_callback(m68k_trace_instr_callback callback)
{
    if (callback) {
        g_trace.instr_callback = callback;
    } else {
        g_trace.instr_callback.reset();
    }
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
        result = (*g_trace.instr_callback)(pc, opcode, g_trace.total_cycles, cycles_executed);
        
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
    
    /* Append to lightweight flow ring for early debugging if enabled */
    if (flow_ring_enabled && flow_ring.size() < flow_ring_limit) {
        flow_ring.push_back(flow_evt{ (uint32_t)type, source_pc, dest_pc, return_addr });
    }

    /* Capture first flow into RAM region (0x100000..0x200000) with D/A regs */
    if (!first_ram_flow_valid && dest_pc >= 0x100000u && dest_pc < 0x200000u) {
        /* Ignore built-in stop-pc sentinel address to avoid false positives */
        if (is_stop_pc_enabled() && dest_pc == get_stop_pc()) {
            /* fall through without capturing */
        } else {
        first_ram_flow_valid = true;
        first_ram_flow_evt = flow_evt{ (uint32_t)type, source_pc, dest_pc, return_addr };
        for (int i = 0; i < 8; i++) {
            first_ram_d[i] = m68k_get_reg(nullptr, static_cast<m68k_register_t>(M68K_REG_D0 + i));
            first_ram_a[i] = m68k_get_reg(nullptr, static_cast<m68k_register_t>(M68K_REG_A0 + i));
        }
        }
        /* Optional diagnostic print; enable with -DM68K_TRACE_RAM_FLOW_LOG=1 */
#ifdef M68K_TRACE_RAM_FLOW_LOG
        printf("[first-ram-flow/native] type=%u src=%x dst=%x ret=%x D=%x,%x,%x,%x,%x,%x,%x,%x A=%x,%x,%x,%x,%x,%x,%x,%x\n",
               (unsigned)first_ram_flow_evt.type,
               (unsigned)first_ram_flow_evt.src,
               (unsigned)first_ram_flow_evt.dst,
               (unsigned)first_ram_flow_evt.ret,
               (unsigned)first_ram_flow_d[0], (unsigned)first_ram_flow_d[1], (unsigned)first_ram_flow_d[2], (unsigned)first_ram_flow_d[3],
               (unsigned)first_ram_flow_d[4], (unsigned)first_ram_flow_d[5], (unsigned)first_ram_flow_d[6], (unsigned)first_ram_flow_d[7],
               (unsigned)first_ram_flow_a[0], (unsigned)first_ram_flow_a[1], (unsigned)first_ram_flow_a[2], (unsigned)first_ram_flow_a[3],
               (unsigned)first_ram_flow_a[4], (unsigned)first_ram_flow_a[5], (unsigned)first_ram_flow_a[6], (unsigned)first_ram_flow_a[7]
        );
#endif
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
        result = (*g_trace.flow_callback)(type, source_pc, dest_pc, return_addr,
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
    /* Append to lightweight mem ring for early debugging if enabled */
    if (mem_ring_enabled && mem_ring.size() < mem_ring_limit) {
        mem_evt ev;
        ev.is_read = (type == M68K_TRACE_MEM_READ) ? 1u : 0u;
        ev.pc = pc;
        ev.addr = address;
        ev.value = value;
        ev.size = size;
        mem_ring.push_back(ev);
    }

    if (g_trace.enabled && g_trace.mem_enabled && g_trace.mem_callback) {
        if (is_address_traced(address)) {
            /* Call callback with protection */
            result = (*g_trace.mem_callback)(type, pc, address, value, size,
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

/* ======================================================================== */
/* =================== WASM EXPORTS FOR RING ACCESS ====================== */
/* ======================================================================== */

extern "C" {
/* Flow ring controls */
void flow_trace_reset(void) {
    flow_ring.clear();
    flow_ring_enabled = false;
    flow_ring_limit = 0;
}
void flow_trace_enable(unsigned int limit) {
    flow_ring_enabled = true;
    flow_ring_limit = limit;
    flow_ring.clear();
}
unsigned int flow_trace_count(void) {
    return (unsigned int)flow_ring.size();
}
unsigned int flow_trace_type(unsigned int index) {
    if (index >= flow_ring.size()) return 0u;
    return flow_ring[index].type;
}
unsigned int flow_trace_src(unsigned int index) {
    if (index >= flow_ring.size()) return 0u;
    return flow_ring[index].src;
}
unsigned int flow_trace_dst(unsigned int index) {
    if (index >= flow_ring.size()) return 0u;
    return flow_ring[index].dst;
}
unsigned int flow_trace_ret(unsigned int index) {
    if (index >= flow_ring.size()) return 0u;
    return flow_ring[index].ret;
}

/* Mem ring controls */
void mem_trace_reset(void) {
    mem_ring.clear();
    mem_ring_enabled = false;
    mem_ring_limit = 0;
}
void mem_trace_enable(unsigned int limit) {
    mem_ring_enabled = true;
    mem_ring_limit = limit;
    mem_ring.clear();
}
unsigned int mem_trace_count(void) {
    return (unsigned int)mem_ring.size();
}
unsigned int mem_trace_is_read(unsigned int index) {
    if (index >= mem_ring.size()) return 0u;
    return mem_ring[index].is_read;
}
unsigned int mem_trace_pc(unsigned int index) {
    if (index >= mem_ring.size()) return 0u;
    return mem_ring[index].pc;
}
unsigned int mem_trace_addr(unsigned int index) {
    if (index >= mem_ring.size()) return 0u;
    return mem_ring[index].addr;
}
unsigned int mem_trace_value(unsigned int index) {
    if (index >= mem_ring.size()) return 0u;
    return mem_ring[index].value;
}
unsigned int mem_trace_size(unsigned int index) {
    if (index >= mem_ring.size()) return 0u;
    return (unsigned int)mem_ring[index].size;
}

/* First RAM-flow snapshot exports */
unsigned int first_ram_flow_has(void) { return first_ram_flow_valid ? 1u : 0u; }
void first_ram_flow_clear(void) { first_ram_flow_valid = false; }
unsigned int first_ram_flow_type(void) { return first_ram_flow_valid ? first_ram_flow_evt.type : 0u; }
unsigned int first_ram_flow_src(void) { return first_ram_flow_valid ? first_ram_flow_evt.src : 0u; }
unsigned int first_ram_flow_dst(void) { return first_ram_flow_valid ? first_ram_flow_evt.dst : 0u; }
unsigned int first_ram_flow_ret(void) { return first_ram_flow_valid ? first_ram_flow_evt.ret : 0u; }
unsigned int first_ram_flow_d(unsigned int idx) { return (first_ram_flow_valid && idx < 8) ? first_ram_d[idx] : 0u; }
unsigned int first_ram_flow_a(unsigned int idx) { return (first_ram_flow_valid && idx < 8) ? first_ram_a[idx] : 0u; }
} // extern "C"
