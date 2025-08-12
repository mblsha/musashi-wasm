/* ======================================================================== */
/* =================== M68K PERFETTO TRACING EXAMPLE ==================== */
/* ======================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m68k.h"
#include "m68k_perfetto.h"

/* External functions from myfunc.cc */
#ifdef __cplusplus
extern "C" {
#endif
    void set_read_mem_func(int (*func)(unsigned int address, int size));
    void set_write_mem_func(void (*func)(unsigned int address, int size, unsigned int value));
    void set_pc_hook_func(int (*func)(unsigned int pc));
#ifdef __cplusplus
}
#endif

/* Test memory for M68K emulation (1MB) */
#define MEMORY_SIZE (1024 * 1024)
static unsigned char test_memory[MEMORY_SIZE];

/* Simple memory access functions */
unsigned int read_mem_8(unsigned int address) {
    if (address < MEMORY_SIZE) {
        return test_memory[address];
    }
    return 0;
}

unsigned int read_mem_16(unsigned int address) {
    if (address + 1 < MEMORY_SIZE) {
        return (test_memory[address] << 8) | test_memory[address + 1];
    }
    return 0;
}

unsigned int read_mem_32(unsigned int address) {
    if (address + 3 < MEMORY_SIZE) {
        return (test_memory[address] << 24) | 
               (test_memory[address + 1] << 16) |
               (test_memory[address + 2] << 8) | 
               test_memory[address + 3];
    }
    return 0;
}

void write_mem_8(unsigned int address, unsigned int value) {
    if (address < MEMORY_SIZE) {
        test_memory[address] = value & 0xFF;
    }
}

void write_mem_16(unsigned int address, unsigned int value) {
    if (address + 1 < MEMORY_SIZE) {
        test_memory[address] = (value >> 8) & 0xFF;
        test_memory[address + 1] = value & 0xFF;
    }
}

void write_mem_32(unsigned int address, unsigned int value) {
    if (address + 3 < MEMORY_SIZE) {
        test_memory[address] = (value >> 24) & 0xFF;
        test_memory[address + 1] = (value >> 16) & 0xFF;
        test_memory[address + 2] = (value >> 8) & 0xFF;
        test_memory[address + 3] = value & 0xFF;
    }
}

/* Memory access wrapper for tracing */
int read_mem_wrapper(unsigned int address, int size) {
    switch (size) {
        case 1: return read_mem_8(address);
        case 2: return read_mem_16(address);
        case 4: return read_mem_32(address);
        default: return 0;
    }
}

void write_mem_wrapper(unsigned int address, int size, unsigned int value) {
    switch (size) {
        case 1: write_mem_8(address, value); break;
        case 2: write_mem_16(address, value); break;
        case 4: write_mem_32(address, value); break;
    }
}

/* PC hook for tracing all instruction execution */
int pc_hook(unsigned int pc) {
    /* Let execution continue - we're just observing */
    return 0;
}

void setup_m68k_test_program() {
    /* Create a simple M68K test program */
    /* Reset vector: SP = 0x1000, PC = 0x400 */
    write_mem_32(0, 0x1000);      /* Initial SP */
    write_mem_32(4, 0x400);       /* Initial PC */
    
    /* Simple test program at 0x400: */
    int pc = 0x400;
    
    /* MOVE.L #$12345678, D0 */
    write_mem_16(pc, 0x203C); pc += 2;  /* MOVE.L #imm32, D0 */
    write_mem_32(pc, 0x12345678); pc += 4;
    
    /* MOVE.L D0, $800 */
    write_mem_16(pc, 0x21C0); pc += 2;  /* MOVE.L D0, abs.w */
    write_mem_16(pc, 0x0800); pc += 2;  /* address $800 */
    
    /* BSR to subroutine at 0x500 */
    write_mem_16(pc, 0x6100); pc += 2;  /* BSR */
    write_mem_16(pc, 0x00F6); pc += 2;  /* offset to 0x500 (0x40C + 0xF6 = 0x502, but BSR adds 2) */
    
    /* NOP and infinite loop */
    write_mem_16(pc, 0x4E71); pc += 2;  /* NOP */
    write_mem_16(pc, 0x60FE); pc += 2;  /* BRA.S -2 (infinite loop) */
    
    /* Subroutine at 0x500: */
    pc = 0x500;
    write_mem_16(pc, 0x5280); pc += 2;  /* ADDQ.L #1, D0 */
    write_mem_16(pc, 0x4E75); pc += 2;  /* RTS */
}

int main(int argc, char* argv[]) {
    printf("M68K Perfetto Tracing Example\n");
    printf("============================\n\n");
    
    /* Check if Perfetto is available */
    if (!m68k_perfetto_is_initialized()) {
        printf("Initializing Perfetto tracing...\n");
        if (m68k_perfetto_init("M68K_Emulator_Example") != 0) {
            #ifdef ENABLE_PERFETTO
                printf("Warning: Failed to initialize Perfetto tracing\n");
                printf("Continuing without Perfetto...\n\n");
            #else
                printf("Perfetto tracing not compiled in (ENABLE_PERFETTO not defined)\n");
                printf("Continuing with CPU emulation only...\n\n");
            #endif
        } else {
            printf("Perfetto tracing initialized successfully!\n\n");
            
            /* Enable all tracing features */
            m68k_perfetto_enable_flow(1);
            m68k_perfetto_enable_memory(1);
            m68k_perfetto_enable_instructions(1);
            printf("Enabled: Flow tracing, Memory tracing, Instruction tracing\n\n");
        }
    }
    
    /* Initialize M68K CPU */
    printf("Initializing M68K CPU...\n");
    m68k_init();
    
    /* Set up memory callbacks */
    set_read_mem_func(read_mem_wrapper);
    set_write_mem_func(write_mem_wrapper);
    set_pc_hook_func(pc_hook);
    
    /* Enable M68K tracing */
    m68k_trace_enable(1);
    printf("M68K CPU initialized and tracing enabled\n\n");
    
    /* Set up test program */
    printf("Setting up test program...\n");
    setup_m68k_test_program();
    
    /* Reset CPU to start execution */
    m68k_pulse_reset();
    printf("CPU reset, starting execution\n\n");
    
    /* Execute some instructions */
    printf("Executing M68K instructions...\n");
    
    /* Execute the program step by step */
    for (int i = 0; i < 10; i++) {
        printf("Execution step %d: PC=0x%08X\n", i + 1, m68k_get_reg(NULL, M68K_REG_PC));
        
        int cycles_executed = m68k_execute(1);  /* Execute 1 cycle */
        if (cycles_executed == 0) {
            printf("CPU halted or error occurred\n");
            break;
        }
        
        /* Show some register state */
        printf("  D0=0x%08X, A7=0x%08X, cycles_executed=%d\n", 
               m68k_get_reg(NULL, M68K_REG_D0),
               m68k_get_reg(NULL, M68K_REG_A7),
               cycles_executed);
    }
    
    printf("\nExecution completed!\n\n");
    
    /* Export Perfetto trace if enabled */
    if (m68k_perfetto_is_initialized()) {
        printf("Exporting Perfetto trace...\n");
        
        /* Try to save to file first */
        if (m68k_perfetto_save_trace("m68k_example_trace.perfetto-trace") == 0) {
            printf("Trace saved to: m68k_example_trace.perfetto-trace\n");
            printf("Open at: https://ui.perfetto.dev\n\n");
        } else {
            printf("Failed to save trace file\n");
        }
        
        /* Also demonstrate raw data export (useful for WASM) */
        uint8_t* trace_data = NULL;
        size_t trace_size = 0;
        
        if (m68k_perfetto_export_trace(&trace_data, &trace_size) == 0 && trace_data) {
            printf("Trace exported as raw data: %zu bytes\n", trace_size);
            printf("First 16 bytes: ");
            for (int i = 0; i < 16 && i < trace_size; i++) {
                printf("%02X ", trace_data[i]);
            }
            printf("\n\n");
            
            /* Free the trace data */
            m68k_perfetto_free_trace_data(trace_data);
        } else {
            printf("Failed to export raw trace data\n");
        }
        
        /* Clean up Perfetto */
        printf("Cleaning up Perfetto...\n");
        m68k_perfetto_destroy();
    }
    
    printf("Example completed successfully!\n");
    printf("\nIf Perfetto was enabled, you can now:\n");
    printf("1. Open https://ui.perfetto.dev in your browser\n");
    printf("2. Click 'Open trace file' and select 'm68k_example_trace.perfetto-trace'\n");
    printf("3. Explore the M68K CPU execution timeline!\n");
    
    return 0;
}