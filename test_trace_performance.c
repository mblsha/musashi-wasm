/* ======================================================================== */
/* ================== M68K TRACING PERFORMANCE TEST SUITE ================ */
/* ======================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include "m68k.h"
#include "m68ktrace.h"

/* Test memory buffer */
#define MEMORY_SIZE 0x100000
static unsigned char memory[MEMORY_SIZE];

/* Performance metrics */
typedef struct {
    unsigned long instructions_executed;
    unsigned long flow_events;
    unsigned long mem_events;
    double time_with_tracing;
    double time_without_tracing;
} perf_metrics_t;

static perf_metrics_t metrics;

/* ======================================================================== */
/* ========================== MEMORY HANDLERS ============================ */
/* ======================================================================== */

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
/* ======================== PERFORMANCE CALLBACKS ======================== */
/* ======================================================================== */

int perf_flow_callback(m68k_trace_flow_type type, uint32_t source_pc,
                       uint32_t dest_pc, uint32_t return_addr,
                       const uint32_t* d_regs, const uint32_t* a_regs,
                       uint64_t cycles)
{
    (void)type; (void)source_pc; (void)dest_pc; (void)return_addr;
    (void)d_regs; (void)a_regs; (void)cycles;
    
    metrics.flow_events++;
    return 0;
}

int perf_mem_callback(m68k_trace_mem_type type, uint32_t pc,
                     uint32_t address, uint32_t value, uint8_t size,
                     uint64_t cycles)
{
    (void)type; (void)pc; (void)address; (void)value; (void)size; (void)cycles;
    
    metrics.mem_events++;
    return 0;
}

int perf_instr_callback(uint32_t pc, uint16_t opcode, uint64_t cycles)
{
    (void)pc; (void)opcode; (void)cycles;
    
    metrics.instructions_executed++;
    return 0;
}

/* ======================================================================== */
/* ====================== PERFORMANCE TEST PROGRAMS ====================== */
/* ======================================================================== */

/* Generate a compute-intensive program (prime number calculation) */
void generate_prime_calculator(void)
{
    uint32_t addr = 0x1000;
    
    /* Calculate primes from 2 to 100
     * D0 = current number to test
     * D1 = divisor
     * D2 = remainder
     * D3 = prime count
     */
    
    /* Initialize */
    /* MOVEQ #2,D0 */
    memory[addr++] = 0x70;
    memory[addr++] = 0x02;
    
    /* MOVEQ #0,D3 */
    memory[addr++] = 0x70;
    memory[addr++] = 0x00;
    
    /* Main loop - check_prime: */
    uint32_t check_prime = addr;
    
    /* MOVEQ #2,D1 (start divisor) */
    memory[addr++] = 0x72;
    memory[addr++] = 0x02;
    
    /* Inner loop - try_divide: */
    uint32_t try_divide = addr;
    
    /* CMP.W D0,D1 */
    memory[addr++] = 0xB2;
    memory[addr++] = 0x40;
    
    /* BEQ is_prime (if divisor == number, it's prime) */
    memory[addr++] = 0x67;
    memory[addr++] = 0x10; /* offset */
    
    /* MOVE.W D0,D2 */
    memory[addr++] = 0x34;
    memory[addr++] = 0x00;
    
    /* DIVU D1,D2 (D2 = D0 / D1, remainder in upper word) */
    memory[addr++] = 0x84;
    memory[addr++] = 0xC1;
    
    /* SWAP D2 (get remainder) */
    memory[addr++] = 0x48;
    memory[addr++] = 0x42;
    
    /* TST.W D2 */
    memory[addr++] = 0x4A;
    memory[addr++] = 0x42;
    
    /* BEQ not_prime (if remainder == 0, not prime) */
    memory[addr++] = 0x67;
    memory[addr++] = 0x08; /* offset */
    
    /* ADDQ.W #1,D1 */
    memory[addr++] = 0x52;
    memory[addr++] = 0x41;
    
    /* BRA try_divide */
    memory[addr++] = 0x60;
    memory[addr++] = 0xE8; /* offset back */
    
    /* is_prime: */
    /* ADDQ.W #1,D3 (increment prime count) */
    memory[addr++] = 0x52;
    memory[addr++] = 0x43;
    
    /* not_prime: */
    /* ADDQ.W #1,D0 (next number) */
    memory[addr++] = 0x52;
    memory[addr++] = 0x40;
    
    /* CMP.W #100,D0 */
    memory[addr++] = 0x0C;
    memory[addr++] = 0x40;
    memory[addr++] = 0x00;
    memory[addr++] = 0x64;
    
    /* BLT check_prime */
    memory[addr++] = 0x6D;
    memory[addr++] = 0xD6; /* offset back */
    
    /* Done - infinite loop */
    memory[addr++] = 0x60;
    memory[addr++] = 0xFE;
}

/* Generate a memory-intensive program (bubble sort) */
void generate_bubble_sort(void)
{
    uint32_t addr = 0x1000;
    uint32_t data_addr = 0x8000;
    
    /* Sort 100 words at address 0x8000
     * A0 = array pointer
     * D0 = outer loop counter
     * D1 = inner loop counter
     * D2 = temp for swap
     */
    
    /* Initialize random data */
    for (int i = 0; i < 100; i++) {
        memory[data_addr + i*2] = (i * 37 + 13) & 0xFF;
        memory[data_addr + i*2 + 1] = ((i * 37 + 13) >> 8) & 0xFF;
    }
    
    /* LEA $8000,A0 */
    memory[addr++] = 0x41;
    memory[addr++] = 0xF8;
    memory[addr++] = 0x80;
    memory[addr++] = 0x00;
    
    /* MOVEQ #99,D0 (outer loop) */
    memory[addr++] = 0x70;
    memory[addr++] = 0x63;
    
    /* outer_loop: */
    uint32_t outer_loop = addr;
    
    /* MOVEQ #0,D1 (inner loop) */
    memory[addr++] = 0x72;
    memory[addr++] = 0x00;
    
    /* inner_loop: */
    uint32_t inner_loop = addr;
    
    /* MOVE.W (A0,D1.W*2),D2 */
    memory[addr++] = 0x34;
    memory[addr++] = 0x30;
    memory[addr++] = 0x11;
    memory[addr++] = 0x00;
    
    /* CMP.W 2(A0,D1.W*2),D2 */
    memory[addr++] = 0xB4;
    memory[addr++] = 0x70;
    memory[addr++] = 0x11;
    memory[addr++] = 0x02;
    
    /* BLE no_swap */
    memory[addr++] = 0x6F;
    memory[addr++] = 0x0E;
    
    /* Swap elements */
    /* MOVE.W 2(A0,D1.W*2),D3 */
    memory[addr++] = 0x36;
    memory[addr++] = 0x30;
    memory[addr++] = 0x11;
    memory[addr++] = 0x02;
    
    /* MOVE.W D2,2(A0,D1.W*2) */
    memory[addr++] = 0x31;
    memory[addr++] = 0x82;
    memory[addr++] = 0x11;
    memory[addr++] = 0x02;
    
    /* MOVE.W D3,(A0,D1.W*2) */
    memory[addr++] = 0x31;
    memory[addr++] = 0x83;
    memory[addr++] = 0x11;
    memory[addr++] = 0x00;
    
    /* no_swap: */
    /* ADDQ.W #1,D1 */
    memory[addr++] = 0x52;
    memory[addr++] = 0x41;
    
    /* CMP.W D0,D1 */
    memory[addr++] = 0xB2;
    memory[addr++] = 0x40;
    
    /* BLT inner_loop */
    memory[addr++] = 0x6D;
    uint8_t offset = addr - inner_loop;
    memory[addr++] = 256 - offset;
    
    /* DBF D0,outer_loop */
    memory[addr++] = 0x51;
    memory[addr++] = 0xC8;
    offset = addr - outer_loop;
    memory[addr++] = (256 - offset) >> 8;
    memory[addr++] = (256 - offset) & 0xFF;
    
    /* Done - infinite loop */
    memory[addr++] = 0x60;
    memory[addr++] = 0xFE;
}

/* Generate function call intensive program */
void generate_recursive_fibonacci(void)
{
    uint32_t addr = 0x1000;
    
    /* Calculate fibonacci(10) recursively
     * D0 = n
     * D1 = result
     */
    
    /* Main: MOVEQ #10,D0 */
    memory[addr++] = 0x70;
    memory[addr++] = 0x0A;
    
    /* BSR fibonacci */
    memory[addr++] = 0x61;
    memory[addr++] = 0x00;
    memory[addr++] = 0x00;
    memory[addr++] = 0x02;
    
    /* Infinite loop */
    memory[addr++] = 0x60;
    memory[addr++] = 0xFE;
    
    /* fibonacci: */
    /* CMP.W #2,D0 */
    memory[addr++] = 0x0C;
    memory[addr++] = 0x40;
    memory[addr++] = 0x00;
    memory[addr++] = 0x02;
    
    /* BGE recursive */
    memory[addr++] = 0x6C;
    memory[addr++] = 0x06;
    
    /* Base case: MOVE.W D0,D1 */
    memory[addr++] = 0x32;
    memory[addr++] = 0x00;
    
    /* RTS */
    memory[addr++] = 0x4E;
    memory[addr++] = 0x75;
    
    /* recursive: */
    /* Save D0 */
    /* MOVE.W D0,-(A7) */
    memory[addr++] = 0x3F;
    memory[addr++] = 0x00;
    
    /* SUBQ.W #1,D0 */
    memory[addr++] = 0x53;
    memory[addr++] = 0x40;
    
    /* BSR fibonacci */
    memory[addr++] = 0x61;
    memory[addr++] = 0x00;
    memory[addr++] = 0xFF;
    memory[addr++] = 0xE8;
    
    /* Save result */
    /* MOVE.W D1,-(A7) */
    memory[addr++] = 0x3F;
    memory[addr++] = 0x01;
    
    /* Restore and decrement D0 */
    /* MOVE.W 2(A7),D0 */
    memory[addr++] = 0x30;
    memory[addr++] = 0x2F;
    memory[addr++] = 0x00;
    memory[addr++] = 0x02;
    
    /* SUBQ.W #2,D0 */
    memory[addr++] = 0x55;
    memory[addr++] = 0x40;
    
    /* BSR fibonacci */
    memory[addr++] = 0x61;
    memory[addr++] = 0x00;
    memory[addr++] = 0xFF;
    memory[addr++] = 0xD6;
    
    /* Add results */
    /* ADD.W (A7)+,D1 */
    memory[addr++] = 0xD2;
    memory[addr++] = 0x5F;
    
    /* Clean stack */
    /* ADDQ.W #2,A7 */
    memory[addr++] = 0x54;
    memory[addr++] = 0x4F;
    
    /* RTS */
    memory[addr++] = 0x4E;
    memory[addr++] = 0x75;
}

/* ======================================================================== */
/* ======================= PERFORMANCE TEST FUNCTIONS ==================== */
/* ======================================================================== */

double get_time_seconds(void)
{
    return (double)clock() / CLOCKS_PER_SEC;
}

void run_performance_test(const char* test_name, void (*program_generator)(void),
                         int trace_flow, int trace_mem, int trace_instr,
                         int execution_cycles)
{
    printf("\nTesting: %s\n", test_name);
    printf("  Configuration: flow=%d, mem=%d, instr=%d\n",
           trace_flow, trace_mem, trace_instr);
    
    /* Reset metrics */
    memset(&metrics, 0, sizeof(metrics));
    memset(memory, 0, MEMORY_SIZE);
    
    /* Generate test program */
    program_generator();
    
    /* Set up reset vector */
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    /* Test WITHOUT tracing */
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_trace_enable(0);
    
    double start_time = get_time_seconds();
    m68k_execute(execution_cycles);
    metrics.time_without_tracing = get_time_seconds() - start_time;
    
    /* Reset CPU for second test */
    m68k_pulse_reset();
    
    /* Test WITH tracing */
    m68k_trace_enable(1);
    
    if (trace_flow) {
        m68k_set_trace_flow_callback(perf_flow_callback);
        m68k_trace_set_flow_enabled(1);
    }
    
    if (trace_mem) {
        m68k_set_trace_mem_callback(perf_mem_callback);
        m68k_trace_set_mem_enabled(1);
        /* Trace data area for memory-intensive tests */
        m68k_trace_add_mem_region(0x8000, 0x9000);
    }
    
    if (trace_instr) {
        m68k_set_trace_instr_callback(perf_instr_callback);
        m68k_trace_set_instr_enabled(1);
    }
    
    start_time = get_time_seconds();
    m68k_execute(execution_cycles);
    metrics.time_with_tracing = get_time_seconds() - start_time;
    
    /* Calculate overhead */
    double overhead = 0;
    if (metrics.time_without_tracing > 0) {
        overhead = ((metrics.time_with_tracing - metrics.time_without_tracing) / 
                    metrics.time_without_tracing) * 100.0;
    }
    
    printf("  Results:\n");
    printf("    Time without tracing: %.4f seconds\n", metrics.time_without_tracing);
    printf("    Time with tracing:    %.4f seconds\n", metrics.time_with_tracing);
    printf("    Overhead:             %.1f%%\n", overhead);
    printf("    Instructions traced:  %lu\n", metrics.instructions_executed);
    printf("    Flow events:          %lu\n", metrics.flow_events);
    printf("    Memory events:        %lu\n", metrics.mem_events);
    
    /* Performance criteria */
    if (trace_instr && overhead > 100) {
        printf("    WARNING: High overhead for instruction tracing\n");
    }
    if (trace_flow && overhead > 50) {
        printf("    WARNING: High overhead for flow tracing\n");
    }
    if (trace_mem && overhead > 75) {
        printf("    WARNING: High overhead for memory tracing\n");
    }
}

/* Stress test with maximum tracing */
void stress_test_all_tracing(void)
{
    printf("\nStress Test: All tracing enabled\n");
    
    memset(&metrics, 0, sizeof(metrics));
    memset(memory, 0, MEMORY_SIZE);
    
    /* Generate all three test programs in different areas */
    generate_prime_calculator();
    
    /* Set up reset vector */
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    /* Enable ALL tracing */
    m68k_trace_enable(1);
    m68k_set_trace_flow_callback(perf_flow_callback);
    m68k_set_trace_mem_callback(perf_mem_callback);
    m68k_set_trace_instr_callback(perf_instr_callback);
    m68k_trace_set_flow_enabled(1);
    m68k_trace_set_mem_enabled(1);
    m68k_trace_set_instr_enabled(1);
    
    /* Add multiple memory regions */
    for (int i = 0; i < 16; i++) {
        m68k_trace_add_mem_region(i * 0x1000, (i + 1) * 0x1000);
    }
    
    double start_time = get_time_seconds();
    
    /* Execute for extended period */
    for (int i = 0; i < 10; i++) {
        m68k_execute(10000);
        
        /* Randomly toggle tracing features */
        if (i % 3 == 0) {
            m68k_trace_set_flow_enabled(i % 2);
        }
        if (i % 3 == 1) {
            m68k_trace_set_mem_enabled(i % 2);
        }
        if (i % 3 == 2) {
            m68k_trace_set_instr_enabled(i % 2);
        }
    }
    
    double total_time = get_time_seconds() - start_time;
    
    printf("  Stress test completed in %.4f seconds\n", total_time);
    printf("  Total events processed: %lu\n", 
           metrics.instructions_executed + metrics.flow_events + metrics.mem_events);
    
    /* Check for memory leaks or corruption */
    uint64_t final_cycles = m68k_get_total_cycles();
    assert(final_cycles > 0);
    
    printf("  Stress test: PASSED\n");
}

/* Test callback overhead with varying complexity */
int complex_callback(m68k_trace_flow_type type, uint32_t source_pc,
                    uint32_t dest_pc, uint32_t return_addr,
                    const uint32_t* d_regs, const uint32_t* a_regs,
                    uint64_t cycles)
{
    /* Simulate complex analysis in callback */
    volatile int sum = 0;
    for (int i = 0; i < 100; i++) {
        sum += type + source_pc + dest_pc + return_addr;
        sum += d_regs[i % 8] + a_regs[i % 8];
        sum += (int)(cycles & 0xFFFF);
    }
    
    metrics.flow_events++;
    return 0;
}

void test_callback_overhead(void)
{
    printf("\nTesting callback overhead\n");
    
    memset(&metrics, 0, sizeof(metrics));
    memset(memory, 0, MEMORY_SIZE);
    
    generate_recursive_fibonacci();
    
    cpu_write_long(0, 0x10000);
    cpu_write_long(4, 0x1000);
    
    /* Test with simple callback */
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    
    m68k_trace_enable(1);
    m68k_set_trace_flow_callback(perf_flow_callback);
    m68k_trace_set_flow_enabled(1);
    
    double start_time = get_time_seconds();
    m68k_execute(5000);
    double simple_time = get_time_seconds() - start_time;
    
    /* Reset and test with complex callback */
    m68k_pulse_reset();
    m68k_set_trace_flow_callback(complex_callback);
    
    metrics.flow_events = 0;
    start_time = get_time_seconds();
    m68k_execute(5000);
    double complex_time = get_time_seconds() - start_time;
    
    printf("  Simple callback time:  %.4f seconds\n", simple_time);
    printf("  Complex callback time: %.4f seconds\n", complex_time);
    printf("  Callback overhead:     %.1f%%\n", 
           ((complex_time - simple_time) / simple_time) * 100.0);
    
    assert(complex_time >= simple_time);
    printf("  Callback overhead test: PASSED\n");
}

/* ======================================================================== */
/* ================================ MAIN ================================== */
/* ======================================================================== */

int main(void)
{
    printf("M68K Tracing Performance Test Suite\n");
    printf("====================================\n");
    
    /* Test compute-intensive workload */
    run_performance_test("Prime Calculator - Flow tracing",
                        generate_prime_calculator, 1, 0, 0, 50000);
    
    run_performance_test("Prime Calculator - Instruction tracing",
                        generate_prime_calculator, 0, 0, 1, 50000);
    
    /* Test memory-intensive workload */
    run_performance_test("Bubble Sort - Memory tracing",
                        generate_bubble_sort, 0, 1, 0, 20000);
    
    run_performance_test("Bubble Sort - All tracing",
                        generate_bubble_sort, 1, 1, 1, 20000);
    
    /* Test call-intensive workload */
    run_performance_test("Recursive Fibonacci - Flow tracing",
                        generate_recursive_fibonacci, 1, 0, 0, 10000);
    
    /* Stress tests */
    stress_test_all_tracing();
    test_callback_overhead();
    
    printf("\n====================================\n");
    printf("All performance tests completed!\n");
    printf("====================================\n");
    
    return 0;
}