/* ======================================================================== */
/* ========================= M68K TRACING TEST SUITE ===================== */
/* ======================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "m68k.h"
#include "m68ktrace.h"

/* Test memory buffer */
#define MEMORY_SIZE 0x100000
static unsigned char memory[MEMORY_SIZE];

/* Trace event counters for testing */
static int trace_flow_calls = 0;
static int trace_mem_reads = 0;
static int trace_mem_writes = 0;
static int trace_instr_count = 0;

/* Last captured trace data */
static struct {
    m68k_trace_flow_type type;
    uint32_t source_pc;
    uint32_t dest_pc;
    uint32_t return_addr;
} last_flow_event;

static struct {
    m68k_trace_mem_type type;
    uint32_t pc;
    uint32_t address;
    uint32_t value;
    uint8_t size;
} last_mem_event;

/* ======================================================================== */
/* ========================== MEMORY HANDLERS ============================ */
/* ======================================================================== */

unsigned int cpu_read_byte(unsigned int address)
{
    if (address < MEMORY_SIZE)
        return memory[address];
    return 0;
}

unsigned int cpu_read_word(unsigned int address)
{
    if (address + 1 < MEMORY_SIZE)
        return (memory[address] << 8) | memory[address + 1];
    return 0;
}

unsigned int cpu_read_long(unsigned int address)
{
    if (address + 3 < MEMORY_SIZE)
        return (memory[address] << 24) | (memory[address + 1] << 16) |
               (memory[address + 2] << 8) | memory[address + 3];
    return 0;
}

void cpu_write_byte(unsigned int address, unsigned int value)
{
    if (address < MEMORY_SIZE)
        memory[address] = value & 0xFF;
}

void cpu_write_word(unsigned int address, unsigned int value)
{
    if (address + 1 < MEMORY_SIZE) {
        memory[address] = (value >> 8) & 0xFF;
        memory[address + 1] = value & 0xFF;
    }
}

void cpu_write_long(unsigned int address, unsigned int value)
{
    if (address + 3 < MEMORY_SIZE) {
        memory[address] = (value >> 24) & 0xFF;
        memory[address + 1] = (value >> 16) & 0xFF;
        memory[address + 2] = (value >> 8) & 0xFF;
        memory[address + 3] = value & 0xFF;
    }
}

/* ======================================================================== */
/* ========================== TRACE CALLBACKS ============================ */
/* ======================================================================== */

int test_flow_callback(m68k_trace_flow_type type, uint32_t source_pc,
                       uint32_t dest_pc, uint32_t return_addr,
                       const uint32_t* d_regs, const uint32_t* a_regs,
                       uint64_t cycles)
{
    (void)d_regs;
    (void)a_regs;
    (void)cycles;
    
    trace_flow_calls++;
    last_flow_event.type = type;
    last_flow_event.source_pc = source_pc;
    last_flow_event.dest_pc = dest_pc;
    last_flow_event.return_addr = return_addr;
    
    return 0; /* Continue execution */
}

int test_mem_callback(m68k_trace_mem_type type, uint32_t pc,
                     uint32_t address, uint32_t value, uint8_t size,
                     uint64_t cycles)
{
    (void)cycles;
    
    if (type == M68K_TRACE_MEM_READ)
        trace_mem_reads++;
    else
        trace_mem_writes++;
    
    last_mem_event.type = type;
    last_mem_event.pc = pc;
    last_mem_event.address = address;
    last_mem_event.value = value;
    last_mem_event.size = size;
    
    return 0; /* Continue execution */
}

int test_instr_callback(uint32_t pc, uint16_t opcode, uint64_t cycles)
{
    (void)pc;
    (void)opcode;
    (void)cycles;
    
    trace_instr_count++;
    return 0; /* Continue execution */
}

/* ======================================================================== */
/* ============================= TEST CASES ============================== */
/* ======================================================================== */

void reset_test_state(void)
{
    trace_flow_calls = 0;
    trace_mem_reads = 0;
    trace_mem_writes = 0;
    trace_instr_count = 0;
    memset(&last_flow_event, 0, sizeof(last_flow_event));
    memset(&last_mem_event, 0, sizeof(last_mem_event));
    memset(memory, 0, MEMORY_SIZE);
}

void test_bsr_tracing(void)
{
    printf("Testing BSR instruction tracing...\n");
    reset_test_state();
    
    /* Set up a simple BSR instruction at address 0x1000 */
    /* BSR.W #$10 (branch to PC+0x10) */
    memory[0x1000] = 0x61; /* BSR opcode */
    memory[0x1001] = 0x00;
    memory[0x1002] = 0x00;
    memory[0x1003] = 0x10;
    
    /* Set up reset vector */
    cpu_write_long(0, 0x10000);     /* Initial stack pointer */
    cpu_write_long(4, 0x1000);      /* Initial program counter */
    
    /* Initialize CPU and tracing */
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_trace_enable(1);
    m68k_set_trace_flow_callback(test_flow_callback);
    m68k_trace_set_flow_enabled(1);
    
    /* Execute the BSR instruction */
    m68k_execute(100);
    
    /* Verify the trace was captured */
    assert(trace_flow_calls > 0);
    assert(last_flow_event.type == M68K_TRACE_FLOW_CALL);
    assert(last_flow_event.source_pc == 0x1000);
    assert(last_flow_event.return_addr == 0x1004);
    
    printf("  BSR tracing: PASSED\n");
}

void test_memory_tracing(void)
{
    printf("Testing memory access tracing...\n");
    reset_test_state();
    
    /* Set up a MOVE instruction at address 0x1000 */
    /* MOVE.W #$1234,($2000) */
    memory[0x1000] = 0x31; /* MOVE.W immediate to absolute */
    memory[0x1001] = 0xFC;
    memory[0x1002] = 0x12; /* Immediate value */
    memory[0x1003] = 0x34;
    memory[0x1004] = 0x00; /* Absolute address */
    memory[0x1005] = 0x00;
    memory[0x1006] = 0x20;
    memory[0x1007] = 0x00;
    
    /* Set up reset vector */
    cpu_write_long(0, 0x10000);     /* Initial stack pointer */
    cpu_write_long(4, 0x1000);      /* Initial program counter */
    
    /* Initialize CPU and tracing */
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_trace_enable(1);
    m68k_set_trace_mem_callback(test_mem_callback);
    m68k_trace_set_mem_enabled(1);
    
    /* Execute the MOVE instruction */
    m68k_execute(100);
    
    /* Verify memory writes were traced */
    assert(trace_mem_writes > 0);
    assert(last_mem_event.type == M68K_TRACE_MEM_WRITE);
    assert(last_mem_event.address == 0x2000);
    assert(last_mem_event.value == 0x1234);
    assert(last_mem_event.size == 2);
    
    printf("  Memory tracing: PASSED\n");
}

void test_selective_memory_regions(void)
{
    printf("Testing selective memory region tracing...\n");
    reset_test_state();
    
    /* Set up two MOVE instructions */
    /* MOVE.W #$1111,($2000) - should be traced */
    /* MOVE.W #$2222,($8000) - should NOT be traced */
    memory[0x1000] = 0x31; /* MOVE.W #$1111,($2000) */
    memory[0x1001] = 0xFC;
    memory[0x1002] = 0x11;
    memory[0x1003] = 0x11;
    memory[0x1004] = 0x00;
    memory[0x1005] = 0x00;
    memory[0x1006] = 0x20;
    memory[0x1007] = 0x00;
    
    memory[0x1008] = 0x31; /* MOVE.W #$2222,($8000) */
    memory[0x1009] = 0xFC;
    memory[0x100A] = 0x22;
    memory[0x100B] = 0x22;
    memory[0x100C] = 0x00;
    memory[0x100D] = 0x00;
    memory[0x100E] = 0x80;
    memory[0x100F] = 0x00;
    
    /* Set up reset vector */
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    /* Initialize CPU and tracing */
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_trace_enable(1);
    m68k_set_trace_mem_callback(test_mem_callback);
    m68k_trace_set_mem_enabled(1);
    
    /* Add selective memory region (only trace 0x2000-0x3000) */
    m68k_trace_add_mem_region(0x2000, 0x3000);
    
    /* Execute both instructions */
    m68k_execute(200);
    
    /* Verify only the first write was traced */
    assert(trace_mem_writes == 1);
    assert(last_mem_event.address == 0x2000);
    assert(last_mem_event.value == 0x1111);
    
    printf("  Selective memory regions: PASSED\n");
}

void test_instruction_tracing(void)
{
    printf("Testing instruction execution tracing...\n");
    reset_test_state();
    
    /* Set up a few NOP instructions */
    for (int i = 0; i < 10; i++) {
        memory[0x1000 + i*2] = 0x4E; /* NOP */
        memory[0x1001 + i*2] = 0x71;
    }
    
    /* Set up reset vector */
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    /* Initialize CPU and tracing */
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_trace_enable(1);
    m68k_set_trace_instr_callback(test_instr_callback);
    m68k_trace_set_instr_enabled(1);
    
    /* Execute several instructions */
    m68k_execute(50);
    
    /* Verify instructions were traced */
    assert(trace_instr_count >= 5);
    
    printf("  Instruction tracing: PASSED\n");
}

void test_cycle_counting(void)
{
    printf("Testing cycle counting...\n");
    reset_test_state();
    
    /* Set up some NOP instructions */
    for (int i = 0; i < 5; i++) {
        memory[0x1000 + i*2] = 0x4E; /* NOP */
        memory[0x1001 + i*2] = 0x71;
    }
    
    /* Set up reset vector */
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    /* Initialize CPU */
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    /* Enable tracing to activate cycle counting */
    m68k_trace_enable(1);
    m68k_reset_total_cycles();
    
    /* Execute some instructions */
    m68k_execute(50);
    
    /* Verify cycles were counted */
    uint64_t cycles = m68k_get_total_cycles();
    assert(cycles > 0);
    
    printf("  Cycle counting: PASSED (counted %llu cycles)\n", 
           (unsigned long long)cycles);
}

/* ======================================================================== */
/* ================================ MAIN ================================== */
/* ======================================================================== */

int main(void)
{
    printf("M68K Tracing Test Suite\n");
    printf("=======================\n\n");
    
    /* Run all tests */
    test_bsr_tracing();
    test_memory_tracing();
    test_selective_memory_regions();
    test_instruction_tracing();
    test_cycle_counting();
    
    printf("\nAll tests PASSED!\n");
    return 0;
}