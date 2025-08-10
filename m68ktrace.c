/* ======================================================================== */
/* ========================= M68K TRACING FRAMEWORK ====================== */
/* ======================================================================== */

#include "m68ktrace.h"
#include "m68k.h"
#include "m68kcpu.h"
#include <string.h>
#include <stdint.h>
#include <limits.h>

#ifndef UINT64_MAX
#define UINT64_MAX ((uint64_t)-1)
#endif

/* ======================================================================== */
/* ========================== INTERNAL STRUCTURES ======================== */
/* ======================================================================== */

#define MAX_TRACE_REGIONS 16

typedef struct {
    /* Global enable flag */
    int enabled;
    
    /* Per-feature enable flags */
    int flow_enabled;
    int mem_enabled;
    int instr_enabled;
    
    /* Callbacks */
    m68k_trace_flow_callback flow_callback;
    m68k_trace_mem_callback mem_callback;
    m68k_trace_instr_callback instr_callback;
    
    /* Memory regions to trace */
    m68k_trace_region mem_regions[MAX_TRACE_REGIONS];
    int num_mem_regions;
    
    /* Cycle counter */
    uint64_t total_cycles;
} m68k_trace_state;

/* Global trace state */
static m68k_trace_state g_trace = {0};

/* ======================================================================== */
/* ========================== INTERNAL FUNCTIONS ========================= */
/* ======================================================================== */

/* Check if an address falls within traced memory regions */
static int is_address_traced(uint32_t address)
{
    int i;
    if (g_trace.num_mem_regions == 0) {
        /* If no regions specified, trace all memory */
        return 1;
    }
    
    for (i = 0; i < g_trace.num_mem_regions; i++) {
        if (address >= g_trace.mem_regions[i].start && 
            address < g_trace.mem_regions[i].end) {
            return 1;
        }
    }
    return 0;
}

/* ======================================================================== */
/* ============================= PUBLIC API ============================== */
/* ======================================================================== */

void m68k_trace_enable(int enable)
{
    g_trace.enabled = enable;
}

int m68k_trace_is_enabled(void)
{
    return g_trace.enabled;
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
    
    if (g_trace.num_mem_regions >= MAX_TRACE_REGIONS) {
        return -1;
    }
    
    /* Check for duplicate regions */
    int i;
    for (i = 0; i < g_trace.num_mem_regions; i++) {
        if (g_trace.mem_regions[i].start == start && 
            g_trace.mem_regions[i].end == end) {
            /* Duplicate region - silently ignore */
            return 0;
        }
    }
    
    g_trace.mem_regions[g_trace.num_mem_regions].start = start;
    g_trace.mem_regions[g_trace.num_mem_regions].end = end;
    g_trace.num_mem_regions++;
    return 0;
}

void m68k_trace_clear_mem_regions(void)
{
    g_trace.num_mem_regions = 0;
}

void m68k_trace_set_flow_enabled(int enable)
{
    g_trace.flow_enabled = enable;
}

void m68k_trace_set_mem_enabled(int enable)
{
    g_trace.mem_enabled = enable;
}

void m68k_trace_set_instr_enabled(int enable)
{
    g_trace.instr_enabled = enable;
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
int m68k_trace_instruction_hook(unsigned int pc)
{
    int result = 0;
    
    /* Check all conditions before calling callback */
    if (g_trace.enabled && g_trace.instr_enabled && g_trace.instr_callback) {
        /* Safely read the opcode at PC */
        uint16_t opcode = 0;
        
        /* Protect against invalid PC */
        if (pc < 0x1000000) { /* Reasonable 68k address space limit */
            opcode = m68k_read_memory_16(pc);
        }
        
        /* Call callback with protection against exceptions */
        result = g_trace.instr_callback(pc, opcode, g_trace.total_cycles);
        
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
        uint32_t d_regs[8];
        uint32_t a_regs[8];
        int i;
        
        for (i = 0; i < 8; i++) {
            d_regs[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + i));
            a_regs[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + i));
        }
        
        /* Call callback with protection */
        result = g_trace.flow_callback(type, source_pc, dest_pc, return_addr,
                                      d_regs, a_regs, g_trace.total_cycles);
        
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
        uint64_t new_total = g_trace.total_cycles + (uint64_t)cycles_executed;
        
        /* Handle overflow by capping at max value */
        if (new_total < g_trace.total_cycles) {
            g_trace.total_cycles = UINT64_MAX;
        } else {
            g_trace.total_cycles = new_total;
        }
    }
}