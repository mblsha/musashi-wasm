/* ======================================================================== */
/* ======================= M68K PERFETTO UNIT TESTS ====================== */
/* ======================================================================== */

#include <gtest/gtest.h>

#include "m68k.h"
#include "m68k_perfetto.h"
#include "m68ktrace.h"

#include <vector>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <fstream>

/* Forward declarations for myfunc.cc wrapper functions */
extern "C" {
    void set_read_mem_func(int (*func)(unsigned int address, int size));
    void set_write_mem_func(void (*func)(unsigned int address, int size, unsigned int value));
    void set_pc_hook_func(int (*func)(unsigned int pc));

    /* Perfetto wrapper functions from myfunc.cc */
    int perfetto_init(const char* process_name);
    void perfetto_destroy(void);
    void perfetto_enable_flow(int enable);
    void perfetto_enable_memory(int enable);
    void perfetto_enable_instructions(int enable);
    int perfetto_export_trace(uint8_t** data_out, size_t* size_out);
    void perfetto_free_trace_data(uint8_t* data);
    int perfetto_save_trace(const char* filename);
    int perfetto_is_initialized(void);
    
    /* Symbol naming functions from myfunc.cc */
    void register_function_name(unsigned int address, const char* name);
    void register_memory_name(unsigned int address, const char* name);
    void register_memory_range(unsigned int start, unsigned int size, const char* name);
    void clear_registered_names(void);
}

class PerfettoTest : public ::testing::Test {
protected:
    static constexpr size_t MEMORY_SIZE = 1024 * 1024; /* 1MB */
    std::vector<uint8_t> memory;

    void SetUp() override {
        memory.resize(MEMORY_SIZE, 0);
        
        /* Initialize M68K */
        m68k_init();
        
        /* Set up memory callbacks */
        ::set_read_mem_func([](unsigned int address, int size) -> int {
            return PerfettoTest::static_read_memory(address, size);
        });
        ::set_write_mem_func([](unsigned int address, int size, unsigned int value) {
            PerfettoTest::static_write_memory(address, size, value);
        });
        ::set_pc_hook_func([](unsigned int pc) -> int {
            (void)pc; /* Suppress unused warning */
            return 0; /* Continue execution */
        });
        
        /* Enable M68K tracing */
        m68k_trace_enable(1);
        
        /* Set up reset vector */
        write_memory_32(0, 0x1000);  /* Initial SP */
        write_memory_32(4, 0x400);   /* Initial PC */
        
        /* Reset CPU */
        m68k_pulse_reset();
        
        /* Run a dummy execution to handle initialization overhead */
        m68k_execute(1);
    }

    void TearDown() override {
        /* Clean up Perfetto if initialized */
        if (::perfetto_is_initialized()) {
            ::perfetto_destroy();
        }
        
        /* Disable tracing */
        m68k_trace_enable(0);
    }
    
    /* Memory access helpers */
    uint32_t read_memory_32(uint32_t address) {
        if (address + 3 < MEMORY_SIZE) {
            return (memory[address] << 24) |
                   (memory[address + 1] << 16) |
                   (memory[address + 2] << 8) |
                   memory[address + 3];
        }
        return 0;
    }
    
    void write_memory_32(uint32_t address, uint32_t value) {
        if (address + 3 < MEMORY_SIZE) {
            memory[address] = (value >> 24) & 0xFF;
            memory[address + 1] = (value >> 16) & 0xFF;
            memory[address + 2] = (value >> 8) & 0xFF;
            memory[address + 3] = value & 0xFF;
        }
    }
    
    void write_memory_16(uint32_t address, uint16_t value) {
        if (address + 1 < MEMORY_SIZE) {
            memory[address] = (value >> 8) & 0xFF;
            memory[address + 1] = value & 0xFF;
        }
    }
    
    /* Static callbacks for C interface */
    static PerfettoTest* current_test;
    
    static int static_read_memory(unsigned int address, int size) {
        if (!current_test) return 0;
        
        switch (size) {
            case 1:
                if (address < current_test->memory.size()) {
                    return current_test->memory[address];
                }
                break;
            case 2:
                if (address + 1 < current_test->memory.size()) {
                    return (current_test->memory[address] << 8) | 
                           current_test->memory[address + 1];
                }
                break;
            case 4:
                return current_test->read_memory_32(address);
        }
        return 0;
    }
    
    static void static_write_memory(unsigned int address, int size, unsigned int value) {
        if (!current_test) return;
        
        switch (size) {
            case 1:
                if (address < current_test->memory.size()) {
                    current_test->memory[address] = value & 0xFF;
                }
                break;
            case 2:
                current_test->write_memory_16(address, value & 0xFFFF);
                break;
            case 4:
                current_test->write_memory_32(address, value);
                break;
        }
    }
    
    void create_simple_program() {
        /* Simple test program at 0x400 */
        uint32_t pc = 0x400;
        
        /* MOVE.L #$12345678, D0 */
        write_memory_16(pc, 0x203C); pc += 2;
        write_memory_32(pc, 0x12345678); pc += 4;
        
        /* NOP */
        write_memory_16(pc, 0x4E71); pc += 2;
        
        /* STOP #$2000 */
        write_memory_16(pc, 0x4E72); pc += 2;
        write_memory_16(pc, 0x2000); pc += 2;
    }
};

/* Static member definition */
PerfettoTest* PerfettoTest::current_test = nullptr;

/* ======================================================================== */
/* ===================== BASIC PERFETTO FUNCTIONALITY ==================== */
/* ======================================================================== */

TEST_F(PerfettoTest, InitializationAndCleanup) {
    PerfettoTest::current_test = this;
    
    /* Test initialization */
    EXPECT_FALSE(::perfetto_is_initialized());
    
    int result = ::perfetto_init("TestEmulator");
    
    #ifdef ENABLE_PERFETTO
        EXPECT_EQ(result, 0);
        EXPECT_TRUE(::perfetto_is_initialized());
        
        /* Test cleanup */
        ::perfetto_destroy();
        EXPECT_FALSE(::perfetto_is_initialized());
    #else
        /* When Perfetto is disabled, functions should be no-ops */
        EXPECT_FALSE(::perfetto_is_initialized());
    #endif
    
    PerfettoTest::current_test = nullptr;
}

TEST_F(PerfettoTest, FeatureEnableDisable) {
    PerfettoTest::current_test = this;
    
    if (::perfetto_init("TestEmulator") == 0) {
        /* These should not crash even when Perfetto is disabled */
        ::perfetto_enable_flow(1);
        ::perfetto_enable_memory(1);
        ::perfetto_enable_instructions(1);
        
        ::perfetto_enable_flow(0);
        ::perfetto_enable_memory(0);
        ::perfetto_enable_instructions(0);
        
        SUCCEED(); /* If we reach here without crashing, test passes */
    }
    
    PerfettoTest::current_test = nullptr;
}

TEST_F(PerfettoTest, TraceExportEmpty) {
    PerfettoTest::current_test = this;
    
    if (::perfetto_init("TestEmulator") == 0) {
        uint8_t* trace_data = nullptr;
        size_t trace_size = 0;
        
        /* Export trace (should work even if empty) */
        int export_result = ::perfetto_export_trace(&trace_data, &trace_size);
        
        #ifdef ENABLE_PERFETTO
            EXPECT_EQ(export_result, 0);
            /* Note: Empty trace might still have some header data */
            if (trace_data) {
                EXPECT_GT(trace_size, 0);
                ::perfetto_free_trace_data(trace_data);
            }
        #else
            EXPECT_EQ(export_result, -1);
            EXPECT_EQ(trace_data, nullptr);
            EXPECT_EQ(trace_size, 0);
        #endif
    }
    
    PerfettoTest::current_test = nullptr;
}

/* ======================================================================== */
/* ====================== M68K INTEGRATION TESTS ========================= */
/* ======================================================================== */

TEST_F(PerfettoTest, BasicInstructionTracing) {
    PerfettoTest::current_test = this;
    
    if (::perfetto_init("M68K_Instruction_Test") != 0) {
        GTEST_SKIP() << "Perfetto not available, skipping instruction tracing test";
    }
    
    /* Enable instruction tracing */
    ::perfetto_enable_instructions(1);
    
    /* Create a simple program */
    create_simple_program();
    m68k_pulse_reset();
    
    /* Execute a few instructions */
    for (int i = 0; i < 3; i++) {
        int cycles = m68k_execute(10);
        if (cycles == 0) break;
    }
    
    /* Export trace */
    uint8_t* trace_data = nullptr;
    size_t trace_size = 0;
    
    int export_result = ::perfetto_export_trace(&trace_data, &trace_size);
    
    #ifdef ENABLE_PERFETTO
        EXPECT_EQ(export_result, 0);
        if (trace_data) {
            EXPECT_GT(trace_size, 0);
            ::perfetto_free_trace_data(trace_data);
        }
    #endif
    
    PerfettoTest::current_test = nullptr;
}

TEST_F(PerfettoTest, FlowTracing) {
    PerfettoTest::current_test = this;
    
    if (::perfetto_init("M68K_Flow_Test") != 0) {
        GTEST_SKIP() << "Perfetto not available, skipping flow tracing test";
    }
    
    /* Enable flow tracing */
    ::perfetto_enable_flow(1);
    
    /* Create program with BSR/RTS */
    uint32_t pc = 0x400;
    
    /* BSR to 0x500 */
    write_memory_16(pc, 0x6100); pc += 2;  /* BSR */
    write_memory_16(pc, 0x00FC); pc += 2;  /* offset to 0x500 */
    
    /* NOP */
    write_memory_16(pc, 0x4E71); pc += 2;
    
    /* Subroutine at 0x500 */
    pc = 0x500;
    write_memory_16(pc, 0x4E75); pc += 2;  /* RTS */
    
    m68k_pulse_reset();
    
    /* Execute the BSR and RTS */
    for (int i = 0; i < 5; i++) {
        int cycles = m68k_execute(10);
        if (cycles == 0) break;
    }
    
    /* Export and verify trace */
    uint8_t* trace_data = nullptr;
    size_t trace_size = 0;
    
    int export_result = ::perfetto_export_trace(&trace_data, &trace_size);
    
    #ifdef ENABLE_PERFETTO
        EXPECT_EQ(export_result, 0);
        if (trace_data) {
            EXPECT_GT(trace_size, 0);
            /* Flow events should have been recorded */
            ::perfetto_free_trace_data(trace_data);
        }
    #endif
    
    PerfettoTest::current_test = nullptr;
}

TEST_F(PerfettoTest, MemoryTracing) {
    PerfettoTest::current_test = this;
    
    if (::perfetto_init("M68K_Memory_Test") != 0) {
        GTEST_SKIP() << "Perfetto not available, skipping memory tracing test";
    }
    
    /* Enable memory tracing */
    ::perfetto_enable_memory(1);
    
    /* Create program that does memory operations */
    uint32_t pc = 0x400;
    
    /* MOVE.L #$DEADBEEF, $800 */
    write_memory_16(pc, 0x21FC); pc += 2;  /* MOVE.L #imm32, abs.w */
    write_memory_32(pc, 0xDEADBEEF); pc += 4; /* immediate value */
    write_memory_16(pc, 0x0800); pc += 2;  /* address $800 */
    
    /* MOVE.L $800, D0 */
    write_memory_16(pc, 0x2038); pc += 2;  /* MOVE.L abs.w, D0 */
    write_memory_16(pc, 0x0800); pc += 2;  /* address $800 */
    
    m68k_pulse_reset();
    
    /* Execute memory operations */
    for (int i = 0; i < 4; i++) {
        int cycles = m68k_execute(10);
        if (cycles == 0) break;
    }
    
    /* Export and verify trace */
    uint8_t* trace_data = nullptr;
    size_t trace_size = 0;
    
    int export_result = ::perfetto_export_trace(&trace_data, &trace_size);
    
    #ifdef ENABLE_PERFETTO
        EXPECT_EQ(export_result, 0);
        if (trace_data) {
            EXPECT_GT(trace_size, 0);
            /* Memory access events should have been recorded */
            ::perfetto_free_trace_data(trace_data);
        }
    #endif
    
    PerfettoTest::current_test = nullptr;
}

/* ======================================================================== */
/* ======================= ERROR HANDLING TESTS ========================== */
/* ======================================================================== */

TEST_F(PerfettoTest, InvalidParameters) {
    PerfettoTest::current_test = this;
    
    /* Test null parameters */
    EXPECT_EQ(::perfetto_export_trace(nullptr, nullptr), -1);
    
    uint8_t* data = nullptr;
    EXPECT_EQ(::perfetto_export_trace(&data, nullptr), -1);
    
    size_t size = 0;
    EXPECT_EQ(::perfetto_export_trace(nullptr, &size), -1);
    
    /* Free null data should not crash */
    ::perfetto_free_trace_data(nullptr);
    
    PerfettoTest::current_test = nullptr;
}

TEST_F(PerfettoTest, MultipleInitialization) {
    PerfettoTest::current_test = this;
    
    int result1 = ::perfetto_init("Test1");
    int result2 = ::perfetto_init("Test2");  /* Should fail */
    
    #ifdef ENABLE_PERFETTO
        EXPECT_EQ(result1, 0);
        EXPECT_EQ(result2, -1);  /* Already initialized */
        
        ::perfetto_destroy();
        
        /* Should be able to initialize again after cleanup */
        int result3 = ::perfetto_init("Test3");
        EXPECT_EQ(result3, 0);
    #endif
    
    PerfettoTest::current_test = nullptr;
}

/* ======================================================================== */
/* ================ COMPREHENSIVE TRACE GENERATION TEST ================== */
/* ======================================================================== */

TEST_F(PerfettoTest, ComplexProgramWithTraceFile) {
    PerfettoTest::current_test = this;
    
    if (::perfetto_init("M68K_Complex_Test") != 0) {
        GTEST_SKIP() << "Perfetto not available, skipping complex trace test";
    }
    
    /* Register function names for the complex test program */
    ::register_function_name(0x400, "main_complex");
    ::register_function_name(0x600, "loop_function");
    ::register_function_name(0x700, "subroutine_1");
    ::register_function_name(0x800, "subroutine_2");
    ::register_memory_name(0x900, "data_buffer");
    ::register_memory_name(0x2000, "test_array");
    
    /* Enable all tracing features */
    ::perfetto_enable_flow(1);
    ::perfetto_enable_memory(1);
    ::perfetto_enable_instructions(1);
    
    /* Build complex program with merge sort, factorial, and nested functions */
    uint32_t pc = 0x400;
    
    /* === Main program at 0x400 === */
    /* Initialize stack pointer */
    write_memory_16(pc, 0x2E7C); pc += 2;  /* MOVE.L #$1000,SP */
    write_memory_32(pc, 0x00001000); pc += 4;
    
    /* Initialize test array at 0x2000 with unsorted data */
    write_memory_16(pc, 0x41F8); pc += 2;  /* LEA $2000,A0 */
    write_memory_16(pc, 0x2000); pc += 2;
    
    /* Store unsorted values */
    write_memory_16(pc, 0x30FC); pc += 2;  /* MOVE.W #$0805,(A0)+ */
    write_memory_16(pc, 0x0805); pc += 2;
    write_memory_16(pc, 0x30FC); pc += 2;  /* MOVE.W #$0302,(A0)+ */
    write_memory_16(pc, 0x0302); pc += 2;
    write_memory_16(pc, 0x30FC); pc += 2;  /* MOVE.W #$0907,(A0)+ */
    write_memory_16(pc, 0x0907); pc += 2;
    write_memory_16(pc, 0x30FC); pc += 2;  /* MOVE.W #$0104,(A0)+ */
    write_memory_16(pc, 0x0104); pc += 2;
    write_memory_16(pc, 0x30FC); pc += 2;  /* MOVE.W #$0606,(A0)+ */
    write_memory_16(pc, 0x0606); pc += 2;
    write_memory_16(pc, 0x30FC); pc += 2;  /* MOVE.W #$0201,(A0)+ */
    write_memory_16(pc, 0x0201); pc += 2;
    write_memory_16(pc, 0x30FC); pc += 2;  /* MOVE.W #$0408,(A0)+ */
    write_memory_16(pc, 0x0408); pc += 2;
    write_memory_16(pc, 0x30FC); pc += 2;  /* MOVE.W #$0503,(A0)+ */
    write_memory_16(pc, 0x0503); pc += 2;
    
    /* Calculate exact BSR offsets - BSR uses PC+2 as base for offset calculation */
    uint32_t call1_pc = pc;
    printf("DEBUG: call1_pc = 0x%X\n", call1_pc);
    
    /* Call simulated merge sort (simplified version) */
    write_memory_16(pc, 0x6100); pc += 2;  /* BSR merge_sort_sim */
    uint32_t offset1 = 0x600 - pc;  /* offset from PC after instruction */
    write_memory_16(pc, offset1 & 0xFFFF); pc += 2;
    printf("DEBUG: BSR to merge_sort: target=0x600, pc_after=0x%X, offset=0x%X\n", pc-2, offset1);
    
    /* Call factorial(5) */
    write_memory_16(pc, 0x303C); pc += 2;  /* MOVE.W #5,D0 */
    write_memory_16(pc, 0x0005); pc += 2;
    uint32_t call2_pc = pc;
    write_memory_16(pc, 0x6100); pc += 2;  /* BSR factorial */
    uint32_t offset2 = 0x700 - pc;  /* offset from PC after instruction */
    write_memory_16(pc, offset2 & 0xFFFF); pc += 2;
    printf("DEBUG: BSR to factorial: target=0x700, pc_after=0x%X, offset=0x%X\n", pc-2, offset2);
    
    /* Store factorial result */
    write_memory_16(pc, 0x31C0); pc += 2;  /* MOVE.W D0,$3000 */
    write_memory_16(pc, 0x3000); pc += 2;
    
    /* Call nested function chain */
    uint32_t call3_pc = pc;
    write_memory_16(pc, 0x6100); pc += 2;  /* BSR func_a */  
    uint32_t offset3 = 0x800 - pc;  /* offset from PC after instruction */
    write_memory_16(pc, offset3 & 0xFFFF); pc += 2;
    printf("DEBUG: BSR to func_a: target=0x800, pc_after=0x%X, offset=0x%X\n", pc-2, offset3);
    
    /* Memory pattern write loop */
    write_memory_16(pc, 0x41F8); pc += 2;  /* LEA $3100,A0 */
    write_memory_16(pc, 0x3100); pc += 2;
    write_memory_16(pc, 0x303C); pc += 2;  /* MOVE.W #15,D0 */
    write_memory_16(pc, 0x000F); pc += 2;
    
    /* Pattern loop */
    uint32_t loop_start = pc;
    write_memory_16(pc, 0x3200); pc += 2;  /* MOVE.W D0,D1 */
    write_memory_16(pc, 0xE149); pc += 2;  /* LSL.W #8,D1 */
    write_memory_16(pc, 0x8240); pc += 2;  /* OR.W D0,D1 */
    write_memory_16(pc, 0x30C1); pc += 2;  /* MOVE.W D1,(A0)+ */
    write_memory_16(pc, 0x51C8); pc += 2;  /* DBF D0,loop */
    write_memory_16(pc, 0xFFF6); pc += 2;  /* -10 bytes back */
    
    /* NOP to let functions complete, then STOP */
    write_memory_16(pc, 0x4E71); pc += 2;  /* NOP - allow functions to return */
    write_memory_16(pc, 0x4E71); pc += 2;  /* NOP */
    write_memory_16(pc, 0x4E71); pc += 2;  /* NOP */
    
    /* STOP after allowing time for returns */
    write_memory_16(pc, 0x4E72); pc += 2;  /* STOP #$2000 */
    write_memory_16(pc, 0x2000); pc += 2;
    
    /* === Simplified merge sort simulation at 0x600 === */
    pc = 0x600;
    /* Simple non-recursive merge sort simulation */
    write_memory_16(pc, 0x48E7); pc += 2;  /* MOVEM.L D0-D3/A0-A2,-(SP) */
    write_memory_16(pc, 0xF0E0); pc += 2;
    
    /* Simple array operations without recursion */
    write_memory_16(pc, 0x2038); pc += 2;  /* MOVE.L $2000,D0 */
    write_memory_16(pc, 0x2000); pc += 2;
    write_memory_16(pc, 0x2238); pc += 2;  /* MOVE.L $2004,D1 */
    write_memory_16(pc, 0x2004); pc += 2;
    write_memory_16(pc, 0xB041); pc += 2;  /* CMP.W D1,D0 */
    write_memory_16(pc, 0x21C0); pc += 2;  /* MOVE.L D0,$2100 */
    write_memory_16(pc, 0x2100); pc += 2;
    
    /* Restore and return */
    write_memory_16(pc, 0x4CDF); pc += 2;  /* MOVEM.L (SP)+,D0-D3/A0-A2 */
    write_memory_16(pc, 0x070F); pc += 2;
    write_memory_16(pc, 0x4E75); pc += 2;  /* RTS */
    
    /* === Simple factorial at 0x700 (no recursion to avoid infinite loops) === */
    pc = 0x700;
    /* Simple factorial(5) = 120 calculation */
    write_memory_16(pc, 0x0C40); pc += 2;  /* CMP.W #1,D0 */
    write_memory_16(pc, 0x0001); pc += 2;
    write_memory_16(pc, 0x6F0C); pc += 2;  /* BLE base_case */
    
    /* Simplified factorial for D0=5: 5*4*3*2*1 = 120 */
    write_memory_16(pc, 0x323C); pc += 2;  /* MOVE.W #4,D1 */
    write_memory_16(pc, 0x0004); pc += 2;
    write_memory_16(pc, 0xC0C1); pc += 2;  /* MULU D1,D0 */  /* D0 = 5*4 = 20 */
    write_memory_16(pc, 0x323C); pc += 2;  /* MOVE.W #6,D1 */
    write_memory_16(pc, 0x0006); pc += 2;  
    write_memory_16(pc, 0xC0C1); pc += 2;  /* MULU D1,D0 */  /* D0 = 20*6 = 120 */
    write_memory_16(pc, 0x4E75); pc += 2;  /* RTS */
    
    /* base_case: */
    write_memory_16(pc, 0x303C); pc += 2;  /* MOVE.W #1,D0 */
    write_memory_16(pc, 0x0001); pc += 2;
    write_memory_16(pc, 0x4E75); pc += 2;  /* RTS */
    
    /* === Simplified nested functions at 0x800 === */
    pc = 0x800;
    /* func_a - simple function with proper RTS */
    write_memory_16(pc, 0x203C); pc += 2;  /* MOVE.L #$AAAA,D0 */
    write_memory_32(pc, 0x0000AAAA); pc += 4;
    write_memory_16(pc, 0x21C0); pc += 2;  /* MOVE.L D0,$3004 */
    write_memory_16(pc, 0x3004); pc += 2;
    
    /* Simple calculation without nested calls to avoid complexity */
    write_memory_16(pc, 0x0680); pc += 2;  /* ADD.L #$1111,D0 */
    write_memory_32(pc, 0x00001111); pc += 4;
    write_memory_16(pc, 0x21C0); pc += 2;  /* MOVE.L D0,$3008 */
    write_memory_16(pc, 0x3008); pc += 2;
    
    /* Store a marker value and return */
    write_memory_16(pc, 0x21FC); pc += 2;  /* MOVE.L #$DEADBEEF,$300C */
    write_memory_32(pc, 0xDEADBEEF); pc += 4;
    write_memory_16(pc, 0x300C); pc += 2;
    
    write_memory_16(pc, 0x4E75); pc += 2;  /* RTS */
    
    /* Reset CPU to execute the program */
    m68k_pulse_reset();
    
    /* Execute the program - run enough cycles for all operations */
    int total_cycles = 0;
    int max_iterations = 1000;  /* Prevent infinite loops */
    
    for (int i = 0; i < max_iterations; i++) {
        int cycles = m68k_execute(100);
        total_cycles += cycles;
        
        /* Check if CPU halted (STOP instruction) or reached invalid memory */
        uint32_t current_pc = m68k_get_reg(NULL, M68K_REG_PC);
        if (cycles == 0 || current_pc >= 0xA000) {  /* Allow execution up to 0xA000 */
            break;
        }
        
        /* Debug: Print PC every few iterations to track execution */
        if (i % 100 == 0) {
            printf("Execution[%d]: PC=0x%08X, cycles=%d\n", i, current_pc, cycles);
        }
    }
    
    EXPECT_GT(total_cycles, 0) << "Program should have executed some cycles";
    
    /* Force cleanup of unclosed slices before saving */
    m68k_perfetto_cleanup_slices();
    
    /* Save trace to file */
    const char* trace_filename = "test_complex_trace.perfetto-trace";
    int save_result = ::perfetto_save_trace(trace_filename);
    
    #ifdef ENABLE_PERFETTO
        EXPECT_EQ(save_result, 0) << "Should successfully save trace";
        
        /* Verify file exists and has content */
        FILE* fp = fopen(trace_filename, "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long file_size = ftell(fp);
            fclose(fp);
            
            EXPECT_GT(file_size, 1024) << "Trace file should have substantial content";
            
            /* Always preserve test_complex_trace.perfetto-trace for analysis */
            // Note: File is preserved for Perfetto UI analysis
        } else {
            ADD_FAILURE() << "Could not open trace file for verification";
        }
    #endif
    
    PerfettoTest::current_test = nullptr;
}

/* ======================================================================== */
/* =============== REAL MERGE SORT BINARY PERFETTO TEST ================== */
/* ======================================================================== */

TEST_F(PerfettoTest, RealMergeSortWithPerfettoTrace) {
    PerfettoTest::current_test = this;
    
    if (::perfetto_init("M68K_Real_MergeSort") != 0) {
        GTEST_SKIP() << "Perfetto not available, skipping real merge sort trace test";
    }
    
    /* Register function names for better trace readability */
    /* Note: These are approximate addresses based on binary layout */
    ::register_function_name(0x400, "main");
    ::register_function_name(0x418, "mergesort");  /* Main merge sort function */
    ::register_function_name(0x450, "merge");      /* Merge helper function */
    
    /* Register memory names for data sections */
    ::register_memory_name(0x4F4, "array_data");      /* The array being sorted */
    ::register_memory_name(0x504, "sorted_flag");     /* Completion flag ($CAFE) */
    ::register_memory_range(0x506, 32, "workspace");  /* Merge workspace */
    
    /* Enable all tracing features for beautiful flame graphs */
    ::perfetto_enable_flow(1);
    ::perfetto_enable_memory(1);
    ::perfetto_enable_instructions(1);
    
    /* Load the real merge sort binary compiled from test_mergesort.s */
    std::ifstream bin_file("tests/test_mergesort.bin", std::ios::binary);
    if (!bin_file) {
        GTEST_SKIP() << "test_mergesort.bin not found - please run 'make' to compile assembly";
    }
    
    /* Get file size */
    bin_file.seekg(0, std::ios::end);
    size_t bin_size = bin_file.tellg();
    bin_file.seekg(0, std::ios::beg);
    
    EXPECT_GT(bin_size, 0) << "Binary file should not be empty";
    EXPECT_LT(bin_size, 4096) << "Binary file should be reasonable size";
    
    /* Load binary starting at 0x400 (where the ORG directive places it) */
    std::vector<uint8_t> binary_data(bin_size);
    bin_file.read(reinterpret_cast<char*>(binary_data.data()), bin_size);
    bin_file.close();
    
    /* Copy binary to memory */
    for (size_t i = 0; i < bin_size && (0x400 + i) < MEMORY_SIZE; i++) {
        memory[0x400 + i] = binary_data[i];
    }
    
    printf("✅ Loaded %zu bytes of real merge sort binary at 0x400\n", bin_size);
    
    /* Show initial array state - should be [8,3,7,1,5,2,6,4] as defined in .s file */
    printf("Initial array at 0x4F4: ");
    for (int i = 0; i < 8; i++) {
        uint16_t val = (memory[0x4F4 + i*2] << 8) | memory[0x4F4 + i*2 + 1];
        printf("%d ", val);
    }
    printf("\n");
    
    /* Set up reset vector for the real program */
    write_memory_32(0, 0x1000);  /* SP */
    write_memory_32(4, 0x400);   /* PC - start of main */
    
    /* Reset CPU */
    m68k_pulse_reset();
    
    printf("🚀 Executing REAL recursive merge sort algorithm...\n");
    
    /* Execute the real merge sort program */
    int total_cycles = 0;
    int iterations = 0;
    const int MAX_ITERATIONS = 1000;
    
    for (iterations = 0; iterations < MAX_ITERATIONS; iterations++) {
        int cycles = m68k_execute(100);
        total_cycles += cycles;
        
        if (cycles == 0) {
            printf("CPU stopped (cycles=0) at iteration %d\n", iterations);
            break;
        }
        
        /* Check if sorted flag is set (0xCAFE at address after array) */
        uint16_t sorted_flag = (memory[0x504] << 8) | memory[0x505];
        if (sorted_flag == 0xCAFE) {
            printf("✅ Real merge sort completed! Flag 0xCAFE detected at iteration %d\n", iterations);
            break;
        }
        
        uint32_t current_pc = m68k_get_reg(NULL, M68K_REG_PC);
        
        /* Progress indicator */
        if (iterations % 100 == 0) {
            printf("Progress: iteration %d, PC=0x%04X, cycles=%d\n", iterations, current_pc, cycles);
        }
        
        /* Safety check */
        if (current_pc < 0x400 || current_pc > 0x600) {
            printf("PC out of expected range: 0x%04X at iteration %d\n", current_pc, iterations);
            break;
        }
    }
    
    printf("\n📊 Real Merge Sort Execution Statistics:\n");
    printf("  Iterations: %d\n", iterations);
    printf("  Total cycles: %d\n", total_cycles);
    printf("  Final PC: 0x%04X\n", m68k_get_reg(NULL, M68K_REG_PC));
    
    /* Show final array state - should be [1,2,3,4,5,6,7,8] */
    printf("Final array at 0x4F4: ");
    for (int i = 0; i < 8; i++) {
        uint16_t val = (memory[0x4F4 + i*2] << 8) | memory[0x4F4 + i*2 + 1];
        printf("%d ", val);
    }
    printf("\n");
    
    /* Verify array is properly sorted */
    bool is_sorted = true;
    for (int i = 1; i < 8; i++) {
        uint16_t prev = (memory[0x4F4 + (i-1)*2] << 8) | memory[0x4F4 + (i-1)*2 + 1];
        uint16_t curr = (memory[0x4F4 + i*2] << 8) | memory[0x4F4 + i*2 + 1];
        if (prev > curr) {
            is_sorted = false;
            printf("❌ Sort failed: position %d has %d > %d\n", i-1, prev, curr);
            break;
        }
    }
    
    EXPECT_TRUE(is_sorted) << "Array should be sorted by the real merge sort algorithm";
    EXPECT_GT(total_cycles, 1000) << "Real merge sort should execute substantial number of cycles";
    
    /* Force cleanup of any unclosed function slices */
    m68k_perfetto_cleanup_slices();
    
    /* Save the REAL merge sort trace */
    const char* real_trace_filename = "real_mergesort.perfetto-trace";
    int save_result = ::perfetto_save_trace(real_trace_filename);
    
    #ifdef ENABLE_PERFETTO
        EXPECT_EQ(save_result, 0) << "Should successfully save real merge sort trace";
        
        /* Verify the trace file */
        FILE* trace_fp = fopen(real_trace_filename, "rb");
        if (trace_fp) {
            fseek(trace_fp, 0, SEEK_END);
            long trace_file_size = ftell(trace_fp);
            fclose(trace_fp);
            
            EXPECT_GT(trace_file_size, 2048) << "Real merge sort trace should be substantial";
            
            printf("\n🔥 SUCCESS: Generated REAL merge sort Perfetto trace!\n");
            printf("📄 File: %s (%ld bytes)\n", real_trace_filename, trace_file_size);
            printf("📈 Upload to https://ui.perfetto.dev to see the recursive merge sort flame graph!\n");
            printf("🎯 Focus on M68K_CPU (Flow) thread for the beautiful recursive call visualization!\n");
            printf("🌟 This shows REAL merge sort with actual BSR/RTS calls and recursive depth!\n");
        } else {
            ADD_FAILURE() << "Could not verify real merge sort trace file";
        }
    #endif
    
    PerfettoTest::current_test = nullptr;
}