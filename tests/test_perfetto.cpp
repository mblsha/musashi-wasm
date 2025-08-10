/* ======================================================================== */
/* ======================= M68K PERFETTO UNIT TESTS ====================== */
/* ======================================================================== */

#include <gtest/gtest.h>

#include "m68k.h"
#include "m68k_perfetto.h"
#include "m68ktrace.h"

#include <vector>
#include <memory>

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