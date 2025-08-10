/* ======================================================================== */
/* ========================= M68K TRACING FRAMEWORK ====================== */
/* ======================================================================== */

#ifndef M68KTRACE__HEADER
#define M68KTRACE__HEADER

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ======================================================================== */
/* ========================= TRACING ENUMERATIONS ======================== */
/* ======================================================================== */

/* Types of control flow events */
typedef enum {
    M68K_TRACE_FLOW_CALL = 0,           /* BSR, JSR */
    M68K_TRACE_FLOW_RETURN,             /* RTS, RTR, RTD */
    M68K_TRACE_FLOW_EXCEPTION_RETURN,   /* RTE */
    M68K_TRACE_FLOW_JUMP,               /* JMP, BRA */
    M68K_TRACE_FLOW_BRANCH_TAKEN,       /* Conditional branches taken */
    M68K_TRACE_FLOW_BRANCH_NOT_TAKEN,   /* Conditional branches not taken */
    M68K_TRACE_FLOW_TRAP,               /* TRAP instruction */
    M68K_TRACE_FLOW_EXCEPTION           /* Hardware exceptions/interrupts */
} m68k_trace_flow_type;

/* Types of memory access events */
typedef enum {
    M68K_TRACE_MEM_READ = 0,
    M68K_TRACE_MEM_WRITE
} m68k_trace_mem_type;

/* ======================================================================== */
/* ========================== CALLBACK SIGNATURES ======================== */
/* ======================================================================== */

/* Control flow trace callback
 * Parameters:
 *   type - Type of control flow event
 *   source_pc - Address of the instruction causing the flow change
 *   dest_pc - Target address (where execution will continue)
 *   return_addr - For calls: the return address pushed to stack (0 for non-calls)
 *   d_regs - Current data register values (D0-D7)
 *   a_regs - Current address register values (A0-A7)
 *   cycles - Total CPU cycles executed so far
 * Return:
 *   0 to continue execution, non-zero to break execution loop
 */
typedef int (*m68k_trace_flow_callback)(
    m68k_trace_flow_type type,
    uint32_t source_pc,
    uint32_t dest_pc,
    uint32_t return_addr,
    const uint32_t* d_regs,
    const uint32_t* a_regs,
    uint64_t cycles
);

/* Memory access trace callback
 * Parameters:
 *   type - READ or WRITE
 *   pc - Address of the instruction performing the access
 *   address - Memory address being accessed
 *   value - Data being written or data that was read
 *   size - Size of access (1, 2, or 4 bytes)
 *   cycles - Total CPU cycles executed so far
 * Return:
 *   0 to continue execution, non-zero to break execution loop
 */
typedef int (*m68k_trace_mem_callback)(
    m68k_trace_mem_type type,
    uint32_t pc,
    uint32_t address,
    uint32_t value,
    uint8_t size,
    uint64_t cycles
);

/* Instruction execution trace callback
 * Called before each instruction execution
 * Parameters:
 *   pc - Address of the instruction about to execute
 *   opcode - The instruction opcode (first word)
 *   start_cycles - Total CPU cycles executed so far
 *   cycles_executed - Number of cycles this instruction will take
 * Return:
 *   0 to continue execution, non-zero to break execution loop
 */
typedef int (*m68k_trace_instr_callback)(
    uint32_t pc,
    uint16_t opcode,
    uint64_t start_cycles,
    int cycles_executed
);

/* ======================================================================== */
/* ========================== TRACING CONFIGURATION ====================== */
/* ======================================================================== */

/* Memory region for selective tracing */
typedef struct {
    uint32_t start;  /* Start address (inclusive) */
    uint32_t end;    /* End address (exclusive) */
} m68k_trace_region;

/* ======================================================================== */
/* ============================= PUBLIC API ============================== */
/* ======================================================================== */

/* Enable/disable tracing system globally */
void m68k_trace_enable(int enable);

/* Check if tracing is enabled */
int m68k_trace_is_enabled(void);

/* Set control flow trace callback */
void m68k_set_trace_flow_callback(m68k_trace_flow_callback callback);

/* Set memory access trace callback */
void m68k_set_trace_mem_callback(m68k_trace_mem_callback callback);

/* Set instruction execution trace callback */
void m68k_set_trace_instr_callback(m68k_trace_instr_callback callback);

/* Add a memory region to trace (up to 16 regions supported) */
/* Returns 0 on success, -1 if too many regions */
int m68k_trace_add_mem_region(uint32_t start, uint32_t end);

/* Clear all memory trace regions */
void m68k_trace_clear_mem_regions(void);

/* Enable/disable specific trace types */
void m68k_trace_set_flow_enabled(int enable);
void m68k_trace_set_mem_enabled(int enable);
void m68k_trace_set_instr_enabled(int enable);

/* Get total cycles executed (useful for timestamping) */
uint64_t m68k_get_total_cycles(void);

/* Reset cycle counter */
void m68k_reset_total_cycles(void);

/* ======================================================================== */
/* ==================== INTERNAL HOOK FUNCTIONS ========================== */
/* ======================================================================== */
/* These are called from the CPU core - not part of public API */

int m68k_trace_instruction_hook(unsigned int pc, uint16_t opcode, int cycles_executed);
int m68k_trace_flow_hook(m68k_trace_flow_type type, uint32_t source_pc, 
                         uint32_t dest_pc, uint32_t return_addr);
int m68k_trace_mem_hook(m68k_trace_mem_type type, uint32_t pc,
                       uint32_t address, uint32_t value, uint8_t size);
void m68k_trace_update_cycles(int cycles_executed);

#ifdef __cplusplus
}
#endif

#endif /* M68KTRACE__HEADER */