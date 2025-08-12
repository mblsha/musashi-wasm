/* ======================================================================== */
/* =================== M68K TRACING API USAGE EXAMPLE ==================== */
/* ======================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m68k.h"
#include "m68ktrace.h"

/* Simple memory for the emulated system */
#define MEMORY_SIZE 0x10000
static unsigned char memory[MEMORY_SIZE];

/* Statistics */
static struct {
    unsigned int total_calls;
    unsigned int total_returns;
    unsigned int total_jumps;
    unsigned int total_branches_taken;
    unsigned int total_branches_not_taken;
    unsigned int total_mem_reads;
    unsigned int total_mem_writes;
    unsigned int total_instructions;
} trace_stats = {0};

/* ======================================================================== */
/* ========================== MEMORY INTERFACE =========================== */
/* ======================================================================== */

/* Bridge functions required by m68k_memory_bridge.cc and m68kcpu.c */
int m68k_instruction_hook_wrapper(unsigned int pc, unsigned int ir, unsigned int cycles) {
    /* Called before each instruction - return 0 to continue, 1 to stop */
    (void)pc; (void)ir; (void)cycles;
    return 0;
}

unsigned int my_read_memory(unsigned int address, int size) {
    address &= (MEMORY_SIZE - 1);
    switch(size) {
        case 1:
            return memory[address];
        case 2:
            return (memory[address] << 8) | memory[address + 1];
        case 4:
            return (memory[address] << 24) | (memory[address + 1] << 16) |
                   (memory[address + 2] << 8) | memory[address + 3];
        default:
            return 0;
    }
}

void my_write_memory(unsigned int address, int size, unsigned int value) {
    address &= (MEMORY_SIZE - 1);
    switch(size) {
        case 1:
            memory[address] = value & 0xFF;
            break;
        case 2:
            memory[address] = (value >> 8) & 0xFF;
            memory[address + 1] = value & 0xFF;
            break;
        case 4:
            memory[address] = (value >> 24) & 0xFF;
            memory[address + 1] = (value >> 16) & 0xFF;
            memory[address + 2] = (value >> 8) & 0xFF;
            memory[address + 3] = value & 0xFF;
            break;
    }
}

unsigned int cpu_read_byte(unsigned int address)
{
    return memory[address & (MEMORY_SIZE - 1)];
}

unsigned int cpu_read_word(unsigned int address)
{
    address &= (MEMORY_SIZE - 1);
    return (memory[address] << 8) | memory[address + 1];
}

unsigned int cpu_read_long(unsigned int address)
{
    address &= (MEMORY_SIZE - 1);
    return (memory[address] << 24) | (memory[address + 1] << 16) |
           (memory[address + 2] << 8) | memory[address + 3];
}

void cpu_write_byte(unsigned int address, unsigned int value)
{
    memory[address & (MEMORY_SIZE - 1)] = value & 0xFF;
}

void cpu_write_word(unsigned int address, unsigned int value)
{
    address &= (MEMORY_SIZE - 1);
    memory[address] = (value >> 8) & 0xFF;
    memory[address + 1] = value & 0xFF;
}

void cpu_write_long(unsigned int address, unsigned int value)
{
    address &= (MEMORY_SIZE - 1);
    memory[address] = (value >> 24) & 0xFF;
    memory[address + 1] = (value >> 16) & 0xFF;
    memory[address + 2] = (value >> 8) & 0xFF;
    memory[address + 3] = value & 0xFF;
}

/* ======================================================================== */
/* =========================== TRACE CALLBACKS =========================== */
/* ======================================================================== */

int trace_control_flow(m68k_trace_flow_type type, uint32_t source_pc,
                       uint32_t dest_pc, uint32_t return_addr,
                       const uint32_t* d_regs, const uint32_t* a_regs,
                       uint64_t cycles)
{
    const char* type_name;
    
    switch(type) {
        case M68K_TRACE_FLOW_CALL:
            type_name = "CALL";
            trace_stats.total_calls++;
            printf("[%8llu] %s: PC=%06X -> %06X (ret=%06X) SP=%08X\n",
                   (unsigned long long)cycles, type_name, source_pc, dest_pc, 
                   return_addr, a_regs[7]);
            break;
            
        case M68K_TRACE_FLOW_RETURN:
            type_name = "RET ";
            trace_stats.total_returns++;
            printf("[%8llu] %s: PC=%06X -> %06X            SP=%08X\n",
                   (unsigned long long)cycles, type_name, source_pc, dest_pc, a_regs[7]);
            break;
            
        case M68K_TRACE_FLOW_EXCEPTION_RETURN:
            type_name = "RTE ";
            printf("[%8llu] %s: PC=%06X -> %06X\n",
                   (unsigned long long)cycles, type_name, source_pc, dest_pc);
            break;
            
        case M68K_TRACE_FLOW_JUMP:
            type_name = "JUMP";
            trace_stats.total_jumps++;
            /* Only log jumps that go backwards (likely loops) or far jumps */
            if (dest_pc < source_pc || abs((int)dest_pc - (int)source_pc) > 0x100) {
                printf("[%8llu] %s: PC=%06X -> %06X\n",
                       (unsigned long long)cycles, type_name, source_pc, dest_pc);
            }
            break;
            
        case M68K_TRACE_FLOW_BRANCH_TAKEN:
            trace_stats.total_branches_taken++;
            break;
            
        case M68K_TRACE_FLOW_BRANCH_NOT_TAKEN:
            trace_stats.total_branches_not_taken++;
            break;
            
        default:
            type_name = "????";
            break;
    }
    
    return 0; /* Continue execution */
}

int trace_memory_access(m68k_trace_mem_type type, uint32_t pc,
                       uint32_t address, uint32_t value, uint8_t size,
                       uint64_t cycles)
{
    /* Only trace accesses to specific regions (e.g., I/O or video memory) */
    if (address >= 0x8000 && address < 0x9000) {
        const char* access_type = (type == M68K_TRACE_MEM_READ) ? "RD" : "WR";
        if (type == M68K_TRACE_MEM_READ) {
            trace_stats.total_mem_reads++;
        } else {
            trace_stats.total_mem_writes++;
        }
        
        printf("[%8llu] MEM %s: PC=%06X addr=%06X val=%0*X size=%d\n",
               (unsigned long long)cycles, access_type, pc, address,
               size * 2, value, size);
    }
    
    return 0; /* Continue execution */
}

int trace_instruction(uint32_t pc, uint16_t opcode, uint64_t start_cycles, int cycles_executed)
{
    trace_stats.total_instructions++;
    
    /* Example: Break execution at specific address */
    if (pc == 0x1234) {
        printf("Breakpoint hit at PC=%06X\n", pc);
        return 1; /* Stop execution */
    }
    
    /* Log every 1000th instruction */
    if (trace_stats.total_instructions % 1000 == 0) {
        printf("[%8llu] Executed %u instructions (PC=%06X)\n",
               (unsigned long long)start_cycles, trace_stats.total_instructions, pc);
    }
    
    return 0; /* Continue execution */
}

/* ======================================================================== */
/* =========================== EXAMPLE PROGRAM =========================== */
/* ======================================================================== */

void load_test_program(void)
{
    unsigned int addr = 0x1000;
    
    /* Simple test program that demonstrates tracing:
     * 
     * main:
     *     BSR     subroutine    ; Call subroutine
     *     MOVE.W  #$1234,$8000  ; Write to traced memory region
     *     MOVE.W  $8000,D0      ; Read from traced memory region
     *     BRA     main          ; Loop forever
     * 
     * subroutine:
     *     NOP
     *     NOP
     *     RTS
     */
    
    /* BSR subroutine (relative offset) */
    memory[addr++] = 0x61;  /* BSR */
    memory[addr++] = 0x00;
    memory[addr++] = 0x00;
    memory[addr++] = 0x0C;  /* Offset to subroutine */
    
    /* MOVE.W #$1234,$8000 */
    memory[addr++] = 0x31;  /* MOVE.W immediate to absolute */
    memory[addr++] = 0xFC;
    memory[addr++] = 0x12;  /* Immediate value */
    memory[addr++] = 0x34;
    memory[addr++] = 0x00;  /* Absolute address */
    memory[addr++] = 0x00;
    memory[addr++] = 0x80;
    memory[addr++] = 0x00;
    
    /* MOVE.W $8000,D0 */
    memory[addr++] = 0x30;  /* MOVE.W from absolute to D0 */
    memory[addr++] = 0x38;
    memory[addr++] = 0x80;  /* Absolute address */
    memory[addr++] = 0x00;
    
    /* BRA main */
    memory[addr++] = 0x60;  /* BRA */
    memory[addr++] = 0xEE;  /* Offset back to main (-18) */
    
    /* Subroutine */
    memory[addr++] = 0x4E;  /* NOP */
    memory[addr++] = 0x71;
    memory[addr++] = 0x4E;  /* NOP */
    memory[addr++] = 0x71;
    memory[addr++] = 0x4E;  /* RTS */
    memory[addr++] = 0x75;
}

/* ======================================================================== */
/* ================================ MAIN ================================== */
/* ======================================================================== */

int main(void)
{
    printf("M68K Tracing Example\n");
    printf("====================\n\n");
    
    /* Initialize memory */
    memset(memory, 0, MEMORY_SIZE);
    
    /* Set up reset vector */
    cpu_write_long(0, 0x2000);     /* Initial stack pointer */
    cpu_write_long(4, 0x1000);     /* Initial program counter */
    
    /* Load test program */
    load_test_program();
    
    /* Initialize CPU */
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    /* Configure tracing */
    printf("Configuring tracing...\n");
    m68k_trace_enable(1);
    
    /* Set up control flow tracing */
    m68k_set_trace_flow_callback(trace_control_flow);
    m68k_trace_set_flow_enabled(1);
    
    /* Set up memory tracing for I/O region (0x8000-0x9000) */
    m68k_set_trace_mem_callback(trace_memory_access);
    m68k_trace_set_mem_enabled(1);
    m68k_trace_add_mem_region(0x8000, 0x9000);
    
    /* Set up instruction tracing */
    m68k_set_trace_instr_callback(trace_instruction);
    m68k_trace_set_instr_enabled(1);
    
    /* Reset cycle counter */
    m68k_reset_total_cycles();
    
    /* Execute program */
    printf("\nExecuting program...\n");
    printf("----------------------------------------\n");
    
    /* Run for 5000 cycles */
    int remaining = 5000;
    while (remaining > 0) {
        int executed = m68k_execute(remaining);
        remaining -= executed;
        
        /* Check if execution was stopped by a trace callback */
        if (executed == 0) {
            printf("Execution stopped by trace callback\n");
            break;
        }
    }
    
    /* Print statistics */
    printf("\n----------------------------------------\n");
    printf("Trace Statistics:\n");
    printf("  Total instructions:    %u\n", trace_stats.total_instructions);
    printf("  Function calls:        %u\n", trace_stats.total_calls);
    printf("  Function returns:      %u\n", trace_stats.total_returns);
    printf("  Jumps:                 %u\n", trace_stats.total_jumps);
    printf("  Branches taken:        %u\n", trace_stats.total_branches_taken);
    printf("  Branches not taken:    %u\n", trace_stats.total_branches_not_taken);
    printf("  Memory reads (traced): %u\n", trace_stats.total_mem_reads);
    printf("  Memory writes (traced):%u\n", trace_stats.total_mem_writes);
    printf("  Total cycles:          %llu\n", (unsigned long long)m68k_get_total_cycles());
    
    return 0;
}
