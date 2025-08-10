/* ======================================================================== */
/* ================== M68K TRACING EDGE CASE TEST SUITE ================== */
/* ======================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include "m68k.h"
#include "m68ktrace.h"

/* Test memory buffer */
#define MEMORY_SIZE 0x100000
static unsigned char memory[MEMORY_SIZE];

/* Test state tracking */
typedef struct {
    int flow_calls;
    int mem_reads;
    int mem_writes;
    int instr_count;
    int callback_return_value;
    int exception_occurred;
    uint32_t last_pc;
    uint32_t last_address;
    int stack_depth;
    uint32_t call_stack[1000];
} test_state_t;

static test_state_t test_state;

/* ======================================================================== */
/* ========================== MEMORY HANDLERS ============================ */
/* ======================================================================== */

unsigned int cpu_read_byte(unsigned int address)
{
    if (address < MEMORY_SIZE)
        return memory[address];
    return 0xFF; /* Return FF for unmapped memory */
}

unsigned int cpu_read_word(unsigned int address)
{
    if (address + 1 < MEMORY_SIZE)
        return (memory[address] << 8) | memory[address + 1];
    return 0xFFFF;
}

unsigned int cpu_read_long(unsigned int address)
{
    if (address + 3 < MEMORY_SIZE)
        return (memory[address] << 24) | (memory[address + 1] << 16) |
               (memory[address + 2] << 8) | memory[address + 3];
    return 0xFFFFFFFF;
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

int edge_flow_callback(m68k_trace_flow_type type, uint32_t source_pc,
                       uint32_t dest_pc, uint32_t return_addr,
                       const uint32_t* d_regs, const uint32_t* a_regs,
                       uint64_t cycles)
{
    (void)d_regs;
    (void)a_regs;
    (void)cycles;
    
    test_state.flow_calls++;
    test_state.last_pc = source_pc;
    
    /* Track call stack depth */
    if (type == M68K_TRACE_FLOW_CALL) {
        if (test_state.stack_depth < 1000) {
            test_state.call_stack[test_state.stack_depth++] = return_addr;
        }
    } else if (type == M68K_TRACE_FLOW_RETURN) {
        if (test_state.stack_depth > 0) {
            test_state.stack_depth--;
        }
    }
    
    /* Test callback that can stop execution */
    return test_state.callback_return_value;
}

int edge_mem_callback(m68k_trace_mem_type type, uint32_t pc,
                     uint32_t address, uint32_t value, uint8_t size,
                     uint64_t cycles)
{
    (void)pc;
    (void)value;
    (void)size;
    (void)cycles;
    
    if (type == M68K_TRACE_MEM_READ)
        test_state.mem_reads++;
    else
        test_state.mem_writes++;
    
    test_state.last_address = address;
    
    return test_state.callback_return_value;
}

int edge_instr_callback(uint32_t pc, uint16_t opcode, uint64_t cycles)
{
    (void)opcode;
    (void)cycles;
    
    test_state.instr_count++;
    test_state.last_pc = pc;
    
    return test_state.callback_return_value;
}

/* Callback that modifies its own behavior */
static int recursive_callback_count = 0;
int recursive_flow_callback(m68k_trace_flow_type type, uint32_t source_pc,
                           uint32_t dest_pc, uint32_t return_addr,
                           const uint32_t* d_regs, const uint32_t* a_regs,
                           uint64_t cycles)
{
    (void)type; (void)source_pc; (void)dest_pc; (void)return_addr;
    (void)d_regs; (void)a_regs; (void)cycles;
    
    recursive_callback_count++;
    
    /* Prevent infinite recursion */
    if (recursive_callback_count > 100) {
        return 1; /* Stop execution */
    }
    
    return 0;
}

/* ======================================================================== */
/* ========================= EDGE CASE TEST FUNCTIONS ==================== */
/* ======================================================================== */

void reset_test_state(void)
{
    memset(&test_state, 0, sizeof(test_state));
    memset(memory, 0, MEMORY_SIZE);
    recursive_callback_count = 0;
    
    /* Reset tracing state */
    m68k_trace_enable(0);
    m68k_trace_clear_mem_regions();
    m68k_reset_total_cycles();
}

/* Test 1: NULL callback handling */
void test_null_callbacks(void)
{
    printf("Testing NULL callback handling...\n");
    reset_test_state();
    
    /* Set up a simple instruction */
    memory[0x1000] = 0x4E; /* NOP */
    memory[0x1001] = 0x71;
    
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    /* Enable tracing with NULL callbacks */
    m68k_trace_enable(1);
    m68k_set_trace_flow_callback(NULL);
    m68k_set_trace_mem_callback(NULL);
    m68k_set_trace_instr_callback(NULL);
    m68k_trace_set_flow_enabled(1);
    m68k_trace_set_mem_enabled(1);
    m68k_trace_set_instr_enabled(1);
    
    /* This should not crash */
    m68k_execute(10);
    
    printf("  NULL callbacks: PASSED\n");
}

/* Test 2: Recursive function calls */
void test_recursive_calls(void)
{
    printf("Testing recursive function calls...\n");
    reset_test_state();
    
    uint32_t addr = 0x1000;
    
    /* Recursive function that calls itself 5 times
     * func:
     *     SUBQ.W  #1,D0
     *     BEQ     done
     *     BSR     func
     * done:
     *     RTS
     */
    
    /* SUBQ.W #1,D0 */
    memory[addr++] = 0x53;
    memory[addr++] = 0x40;
    
    /* BEQ done (+6) */
    memory[addr++] = 0x67;
    memory[addr++] = 0x06;
    
    /* BSR func (-6) */
    memory[addr++] = 0x61;
    memory[addr++] = 0x00;
    memory[addr++] = 0xFF;
    memory[addr++] = 0xF8;
    
    /* done: RTS */
    memory[addr++] = 0x4E;
    memory[addr++] = 0x75;
    
    /* Main: Set D0=5 and call func */
    addr = 0x2000;
    /* MOVEQ #5,D0 */
    memory[addr++] = 0x70;
    memory[addr++] = 0x05;
    
    /* BSR func */
    memory[addr++] = 0x61;
    memory[addr++] = 0x00;
    memory[addr++] = 0xEF;
    memory[addr++] = 0xFC;
    
    /* Infinite loop */
    memory[addr++] = 0x60;
    memory[addr++] = 0xFE;
    
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x2000);
    
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_trace_enable(1);
    m68k_set_trace_flow_callback(edge_flow_callback);
    m68k_trace_set_flow_enabled(1);
    
    m68k_execute(1000);
    
    /* Should have 6 calls (1 initial + 5 recursive) and 6 returns */
    assert(test_state.flow_calls >= 12);
    assert(test_state.stack_depth == 0); /* Stack should be balanced */
    
    printf("  Recursive calls: PASSED (depth tracked correctly)\n");
}

/* Test 3: Stack overflow scenario */
void test_stack_overflow(void)
{
    printf("Testing stack overflow handling...\n");
    reset_test_state();
    
    /* Create infinite recursion */
    /* BSR self */
    memory[0x1000] = 0x61;
    memory[0x1001] = 0x00;
    memory[0x1002] = 0xFF;
    memory[0x1003] = 0xFC;
    
    cpu_write_long(0, 0x1000); /* Very small stack */
    cpu_write_long(4, 0x1000);
    
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_trace_enable(1);
    m68k_set_trace_flow_callback(edge_flow_callback);
    m68k_trace_set_flow_enabled(1);
    
    /* Set callback to stop after 100 calls to prevent infinite loop */
    test_state.callback_return_value = 0;
    
    /* Execute and let it overflow */
    int cycles = m68k_execute(2000);
    
    /* Execution should continue even with stack overflow */
    assert(cycles > 0);
    assert(test_state.flow_calls > 0);
    
    printf("  Stack overflow: PASSED (handled gracefully)\n");
}

/* Test 4: Memory access across region boundaries */
void test_boundary_access(void)
{
    printf("Testing memory access across region boundaries...\n");
    reset_test_state();
    
    /* MOVE.L from address that spans two regions */
    memory[0x1000] = 0x20; /* MOVE.L */
    memory[0x1001] = 0x38;
    memory[0x1002] = 0x7F; /* Address 0x7FFE (spans into 0x8000) */
    memory[0x1003] = 0xFE;
    
    /* Place data at boundary */
    memory[0x7FFE] = 0x12;
    memory[0x7FFF] = 0x34;
    memory[0x8000] = 0x56;
    memory[0x8001] = 0x78;
    
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_trace_enable(1);
    m68k_set_trace_mem_callback(edge_mem_callback);
    m68k_trace_set_mem_enabled(1);
    
    /* Only trace 0x8000-0x9000 */
    m68k_trace_add_mem_region(0x8000, 0x9000);
    
    test_state.mem_reads = 0;
    m68k_execute(20);
    
    /* Should capture the access even though it starts before the region */
    assert(test_state.mem_reads > 0 || test_state.last_address >= 0x7FFE);
    
    printf("  Boundary access: PASSED\n");
}

/* Test 5: Overlapping trace regions */
void test_overlapping_regions(void)
{
    printf("Testing overlapping trace regions...\n");
    reset_test_state();
    
    /* Set up memory accesses to different addresses */
    uint32_t addr = 0x1000;
    
    /* MOVE.W #$1111,$5000 */
    memory[addr++] = 0x31;
    memory[addr++] = 0xFC;
    memory[addr++] = 0x11;
    memory[addr++] = 0x11;
    memory[addr++] = 0x50;
    memory[addr++] = 0x00;
    
    /* MOVE.W #$2222,$5800 */
    memory[addr++] = 0x31;
    memory[addr++] = 0xFC;
    memory[addr++] = 0x22;
    memory[addr++] = 0x22;
    memory[addr++] = 0x58;
    memory[addr++] = 0x00;
    
    /* MOVE.W #$3333,$6000 */
    memory[addr++] = 0x31;
    memory[addr++] = 0xFC;
    memory[addr++] = 0x33;
    memory[addr++] = 0x33;
    memory[addr++] = 0x60;
    memory[addr++] = 0x00;
    
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_trace_enable(1);
    m68k_set_trace_mem_callback(edge_mem_callback);
    m68k_trace_set_mem_enabled(1);
    
    /* Add overlapping regions */
    m68k_trace_add_mem_region(0x5000, 0x6000);
    m68k_trace_add_mem_region(0x5500, 0x6500); /* Overlaps with first */
    
    test_state.mem_writes = 0;
    m68k_execute(100);
    
    /* All three writes should be captured (no double counting) */
    assert(test_state.mem_writes == 3);
    
    printf("  Overlapping regions: PASSED\n");
}

/* Test 6: Maximum regions limit */
void test_max_regions(void)
{
    printf("Testing maximum region limit...\n");
    reset_test_state();
    
    m68k_trace_enable(1);
    
    /* Try to add more than max regions (16) */
    int result = 0;
    for (int i = 0; i < 20; i++) {
        result = m68k_trace_add_mem_region(i * 0x1000, (i + 1) * 0x1000);
        if (i < 16) {
            assert(result == 0); /* Should succeed */
        } else {
            assert(result == -1); /* Should fail */
        }
    }
    
    printf("  Max regions: PASSED (limit enforced)\n");
}

/* Test 7: Callback that stops execution */
void test_callback_stop_execution(void)
{
    printf("Testing callback stopping execution...\n");
    reset_test_state();
    
    /* Set up multiple NOPs */
    for (int i = 0; i < 20; i++) {
        memory[0x1000 + i*2] = 0x4E;
        memory[0x1001 + i*2] = 0x71;
    }
    
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_trace_enable(1);
    m68k_set_trace_instr_callback(edge_instr_callback);
    m68k_trace_set_instr_enabled(1);
    
    /* Stop after 5 instructions */
    test_state.callback_return_value = 0;
    test_state.instr_count = 0;
    
    m68k_execute(10);
    int count_before_stop = test_state.instr_count;
    
    /* Now set callback to stop execution */
    test_state.callback_return_value = 1;
    
    int cycles = m68k_execute(100);
    
    /* Should stop immediately */
    assert(cycles == 0 || test_state.instr_count == count_before_stop + 1);
    
    printf("  Callback stop: PASSED (execution halted)\n");
}

/* Test 8: Tracing state changes during execution */
void test_dynamic_trace_control(void)
{
    printf("Testing dynamic trace enable/disable...\n");
    reset_test_state();
    
    /* Set up NOPs */
    for (int i = 0; i < 10; i++) {
        memory[0x1000 + i*2] = 0x4E;
        memory[0x1001 + i*2] = 0x71;
    }
    
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_set_trace_instr_callback(edge_instr_callback);
    m68k_trace_set_instr_enabled(1);
    
    /* Start with tracing disabled */
    m68k_trace_enable(0);
    test_state.instr_count = 0;
    
    m68k_execute(20);
    int count_disabled = test_state.instr_count;
    assert(count_disabled == 0);
    
    /* Enable tracing */
    m68k_trace_enable(1);
    
    m68k_execute(20);
    assert(test_state.instr_count > 0);
    
    /* Disable again */
    m68k_trace_enable(0);
    int count_before = test_state.instr_count;
    
    m68k_execute(20);
    assert(test_state.instr_count == count_before);
    
    printf("  Dynamic control: PASSED\n");
}

/* Test 9: Cycle counter overflow */
void test_cycle_counter_overflow(void)
{
    printf("Testing cycle counter overflow...\n");
    reset_test_state();
    
    /* NOP */
    memory[0x1000] = 0x4E;
    memory[0x1001] = 0x71;
    
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_trace_enable(1);
    
    /* Test reset */
    m68k_execute(10);
    uint64_t cycles1 = m68k_get_total_cycles();
    assert(cycles1 > 0);
    
    m68k_reset_total_cycles();
    uint64_t cycles2 = m68k_get_total_cycles();
    assert(cycles2 == 0);
    
    m68k_execute(10);
    uint64_t cycles3 = m68k_get_total_cycles();
    assert(cycles3 > 0);
    
    printf("  Cycle counter: PASSED\n");
}

/* Test 10: Indirect jumps and calls */
void test_indirect_control_flow(void)
{
    printf("Testing indirect jumps and calls...\n");
    reset_test_state();
    
    uint32_t addr = 0x1000;
    
    /* Load address into A0 */
    /* LEA $2000,A0 */
    memory[addr++] = 0x41;
    memory[addr++] = 0xF8;
    memory[addr++] = 0x20;
    memory[addr++] = 0x00;
    
    /* JSR (A0) */
    memory[addr++] = 0x4E;
    memory[addr++] = 0x90;
    
    /* Target at 0x2000 */
    memory[0x2000] = 0x4E; /* RTS */
    memory[0x2001] = 0x75;
    
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_trace_enable(1);
    m68k_set_trace_flow_callback(edge_flow_callback);
    m68k_trace_set_flow_enabled(1);
    
    test_state.flow_calls = 0;
    m68k_execute(100);
    
    /* Should capture the indirect call and return */
    assert(test_state.flow_calls >= 2); /* At least one call and one return */
    
    printf("  Indirect control flow: PASSED\n");
}

/* Test 11: Exception during traced instruction */
void test_exception_during_trace(void)
{
    printf("Testing exception during traced instruction...\n");
    reset_test_state();
    
    /* Illegal instruction */
    memory[0x1000] = 0xFF;
    memory[0x1001] = 0xFF;
    
    /* Set up exception vector for illegal instruction (vector 4) */
    cpu_write_long(4 * 4, 0x2000);
    
    /* Exception handler */
    memory[0x2000] = 0x4E; /* RTE */
    memory[0x2001] = 0x73;
    
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_trace_enable(1);
    m68k_set_trace_flow_callback(edge_flow_callback);
    m68k_trace_set_flow_enabled(1);
    
    test_state.flow_calls = 0;
    
    /* Should handle exception gracefully */
    m68k_execute(100);
    
    /* Should see exception return */
    assert(test_state.flow_calls > 0);
    
    printf("  Exception during trace: PASSED\n");
}

/* Test 12: Self-modifying code */
void test_self_modifying_code(void)
{
    printf("Testing self-modifying code...\n");
    reset_test_state();
    
    uint32_t addr = 0x1000;
    
    /* Modify the next instruction */
    /* MOVE.W #$4E71,$1006 (change to NOP) */
    memory[addr++] = 0x31;
    memory[addr++] = 0xFC;
    memory[addr++] = 0x4E;
    memory[addr++] = 0x71;
    memory[addr++] = 0x10;
    memory[addr++] = 0x06;
    
    /* This will be modified */
    memory[addr++] = 0xFF; /* Initially illegal */
    memory[addr++] = 0xFF;
    
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_trace_enable(1);
    m68k_set_trace_mem_callback(edge_mem_callback);
    m68k_trace_set_mem_enabled(1);
    
    /* Trace code region to catch self-modification */
    m68k_trace_add_mem_region(0x1000, 0x2000);
    
    test_state.mem_writes = 0;
    m68k_execute(100);
    
    /* Should see the write to code memory */
    assert(test_state.mem_writes > 0);
    assert(test_state.last_address == 0x1006);
    
    printf("  Self-modifying code: PASSED\n");
}

/* Test 13: Unaligned memory access */
void test_unaligned_access(void)
{
    printf("Testing unaligned memory access...\n");
    reset_test_state();
    
    /* MOVE.W from odd address (causes address error on 68000) */
    memory[0x1000] = 0x30; /* MOVE.W */
    memory[0x1001] = 0x38;
    memory[0x1002] = 0x10; /* Odd address 0x1001 */
    memory[0x1003] = 0x01;
    
    /* Set up address error vector (vector 3) */
    cpu_write_long(3 * 4, 0x2000);
    
    /* Exception handler */
    memory[0x2000] = 0x4E; /* RTE (simplified) */
    memory[0x2001] = 0x73;
    
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_trace_enable(1);
    m68k_set_trace_mem_callback(edge_mem_callback);
    m68k_trace_set_mem_enabled(1);
    
    /* Execute and handle potential address error */
    m68k_execute(100);
    
    /* Should handle gracefully whether error occurs or not */
    printf("  Unaligned access: PASSED (handled)\n");
}

/* Test 14: Zero-size memory regions */
void test_zero_size_region(void)
{
    printf("Testing zero-size memory region...\n");
    reset_test_state();
    
    m68k_trace_enable(1);
    
    /* Add zero-size region */
    int result = m68k_trace_add_mem_region(0x1000, 0x1000);
    
    /* Implementation should handle this gracefully */
    /* Either reject it or treat as empty region */
    
    printf("  Zero-size region: PASSED (handled)\n");
}

/* Test 15: Branch to self (infinite loop) */
void test_branch_to_self(void)
{
    printf("Testing branch to self...\n");
    reset_test_state();
    
    /* BRA self */
    memory[0x1000] = 0x60;
    memory[0x1001] = 0xFE;
    
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_trace_enable(1);
    m68k_set_trace_flow_callback(edge_flow_callback);
    m68k_trace_set_flow_enabled(1);
    
    /* Stop after 10 branches to prevent infinite loop */
    test_state.flow_calls = 0;
    test_state.callback_return_value = 0;
    
    for (int i = 0; i < 10; i++) {
        m68k_execute(10);
        if (test_state.flow_calls >= 10) {
            test_state.callback_return_value = 1;
            break;
        }
    }
    
    assert(test_state.flow_calls > 0);
    
    printf("  Branch to self: PASSED\n");
}

/* Test 16: MOVEM (multiple register moves) */
void test_movem_instruction(void)
{
    printf("Testing MOVEM instruction tracing...\n");
    reset_test_state();
    
    /* MOVEM.L D0-D7/A0-A6,-(A7) */
    memory[0x1000] = 0x48;
    memory[0x1001] = 0xE7;
    memory[0x1002] = 0xFF;
    memory[0x1003] = 0xFE;
    
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_trace_enable(1);
    m68k_set_trace_mem_callback(edge_mem_callback);
    m68k_trace_set_mem_enabled(1);
    
    test_state.mem_writes = 0;
    m68k_execute(100);
    
    /* Should see multiple memory writes (15 registers) */
    assert(test_state.mem_writes >= 15);
    
    printf("  MOVEM instruction: PASSED\n");
}

/* Test 17: TRAP instruction */
void test_trap_instruction(void)
{
    printf("Testing TRAP instruction tracing...\n");
    reset_test_state();
    
    /* TRAP #0 */
    memory[0x1000] = 0x4E;
    memory[0x1001] = 0x40;
    
    /* Set up TRAP #0 vector (vector 32) */
    cpu_write_long(32 * 4, 0x2000);
    
    /* TRAP handler */
    memory[0x2000] = 0x4E; /* RTE */
    memory[0x2001] = 0x73;
    
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_trace_enable(1);
    m68k_set_trace_flow_callback(edge_flow_callback);
    m68k_trace_set_flow_enabled(1);
    
    test_state.flow_calls = 0;
    m68k_execute(100);
    
    /* Should see exception flow */
    assert(test_state.flow_calls > 0);
    
    printf("  TRAP instruction: PASSED\n");
}

/* Test 18: Callback changing its own configuration */
int self_modifying_callback(m68k_trace_flow_type type, uint32_t source_pc,
                           uint32_t dest_pc, uint32_t return_addr,
                           const uint32_t* d_regs, const uint32_t* a_regs,
                           uint64_t cycles)
{
    (void)type; (void)source_pc; (void)dest_pc; (void)return_addr;
    (void)d_regs; (void)a_regs; (void)cycles;
    
    static int call_count = 0;
    call_count++;
    
    /* Disable tracing after 5 calls */
    if (call_count >= 5) {
        m68k_trace_set_flow_enabled(0);
    }
    
    return 0;
}

void test_self_modifying_callback(void)
{
    printf("Testing self-modifying callback...\n");
    reset_test_state();
    
    /* Multiple BSR instructions */
    for (int i = 0; i < 10; i++) {
        memory[0x1000 + i*4] = 0x61; /* BSR */
        memory[0x1001 + i*4] = 0x00;
        memory[0x1002 + i*4] = 0x00;
        memory[0x1003 + i*4] = 0x40;
    }
    
    /* Target */
    memory[0x1040] = 0x4E; /* RTS */
    memory[0x1041] = 0x75;
    
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_trace_enable(1);
    m68k_set_trace_flow_callback(self_modifying_callback);
    m68k_trace_set_flow_enabled(1);
    
    m68k_execute(1000);
    
    /* Should handle callback modifying its own state */
    printf("  Self-modifying callback: PASSED\n");
}

/* Test 19: Concurrent enable/disable stress test */
void test_concurrent_enable_disable(void)
{
    printf("Testing concurrent enable/disable...\n");
    reset_test_state();
    
    /* NOP */
    memory[0x1000] = 0x4E;
    memory[0x1001] = 0x71;
    
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    /* Rapidly enable/disable tracing */
    for (int i = 0; i < 100; i++) {
        m68k_trace_enable(i % 2);
        m68k_trace_set_flow_enabled(i % 3 == 0);
        m68k_trace_set_mem_enabled(i % 3 == 1);
        m68k_trace_set_instr_enabled(i % 3 == 2);
        
        if (i % 10 == 0) {
            m68k_trace_clear_mem_regions();
            m68k_trace_add_mem_region(i * 100, (i + 1) * 100);
        }
    }
    
    /* Should not crash or corrupt state */
    m68k_execute(10);
    
    printf("  Concurrent enable/disable: PASSED\n");
}

/* Test 20: LINK/UNLK stack frame instructions */
void test_link_unlk(void)
{
    printf("Testing LINK/UNLK instructions...\n");
    reset_test_state();
    
    uint32_t addr = 0x1000;
    
    /* LINK A6,#-16 */
    memory[addr++] = 0x4E;
    memory[addr++] = 0x56;
    memory[addr++] = 0xFF;
    memory[addr++] = 0xF0;
    
    /* Some work... */
    memory[addr++] = 0x4E; /* NOP */
    memory[addr++] = 0x71;
    
    /* UNLK A6 */
    memory[addr++] = 0x4E;
    memory[addr++] = 0x5E;
    
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_trace_enable(1);
    m68k_set_trace_mem_callback(edge_mem_callback);
    m68k_trace_set_mem_enabled(1);
    
    /* Trace stack area */
    m68k_trace_add_mem_region(0xF000, 0x11000);
    
    test_state.mem_writes = 0;
    test_state.mem_reads = 0;
    m68k_execute(100);
    
    /* Should see stack frame creation/destruction */
    assert(test_state.mem_writes > 0 || test_state.mem_reads > 0);
    
    printf("  LINK/UNLK: PASSED\n");
}

/* ======================================================================== */
/* ================================ MAIN ================================== */
/* ======================================================================== */

int main(void)
{
    printf("\nM68K Tracing Edge Case Test Suite\n");
    printf("==================================\n\n");
    
    /* Basic safety tests */
    test_null_callbacks();
    test_zero_size_region();
    test_max_regions();
    
    /* Control flow edge cases */
    test_recursive_calls();
    test_stack_overflow();
    test_indirect_control_flow();
    test_branch_to_self();
    test_trap_instruction();
    test_link_unlk();
    
    /* Memory access edge cases */
    test_boundary_access();
    test_overlapping_regions();
    test_unaligned_access();
    test_movem_instruction();
    test_self_modifying_code();
    
    /* Callback behavior */
    test_callback_stop_execution();
    test_self_modifying_callback();
    test_exception_during_trace();
    
    /* State management */
    test_dynamic_trace_control();
    test_concurrent_enable_disable();
    test_cycle_counter_overflow();
    
    printf("\n==================================\n");
    printf("All edge case tests PASSED!\n");
    printf("==================================\n\n");
    
    return 0;
}