/* ======================================================================== */
/* ======================= M68K PERFETTO UNIT TESTS ====================== */
/* ======================================================================== */

#include "m68k_test_common.h"
#include "m68k_perfetto.h"
#include "m68ktrace.h"

#include <memory>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <set>

/* Forward declarations for myfunc.cc wrapper functions */
extern "C" {
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

struct FlowEvent {
    m68k_trace_flow_type type;
    uint32_t source_pc;
    uint32_t dest_pc;
};

static std::vector<FlowEvent> g_flow_events;

static int CaptureFlowCallback(
    m68k_trace_flow_type type,
    uint32_t source_pc,
    uint32_t dest_pc,
    uint32_t return_addr,
    const uint32_t* d_regs,
    const uint32_t* a_regs,
    uint64_t cycles
) {
    (void)return_addr;
    (void)d_regs;
    (void)a_regs;
    (void)cycles;

    g_flow_events.push_back(FlowEvent{
        type,
        source_pc,
        dest_pc
    });
    return 0;
}

/* Define test class using the minimal base */
DECLARE_M68K_TEST(PerfettoTest) {
protected:
    void OnSetUp() override {
        /* Enable M68K tracing for Perfetto tests */
        m68k_trace_enable(1);
        
        /* Set proper PC for tests */
        write_long(4, 0x400);   /* Initial PC */
        m68k_pulse_reset();
    }
    
    void OnTearDown() override {
        /* Clean up Perfetto if initialized */
        if (::perfetto_is_initialized()) {
            ::perfetto_destroy();
        }
        
        /* Disable tracing */
        m68k_trace_enable(0);
    }
    
    void create_simple_program() {
        /* Simple test program at 0x400 */
        uint32_t pc = 0x400;
        
        /* MOVE.L #$12345678, D0 */
        write_word(pc, 0x203C); pc += 2;
        write_long(pc, 0x12345678); pc += 4;
        
        /* NOP */
        write_word(pc, 0x4E71); pc += 2;
        
        /* STOP #$2000 */
        write_word(pc, 0x4E72); pc += 2;
        write_word(pc, 0x2000); pc += 2;
    }
    
    void create_flow_program() {
        uint32_t pc = 0x400;

        /* MOVEQ #1, D0 */
        write_word(pc, 0x7001); pc += 2;
        /* TST.B D0 (sets Z=0) */
        write_word(pc, 0x4A00); pc += 2;
        /* BNE.s to skip the next two NOPs (displacement +4 -> target 0x40A) */
        write_word(pc, 0x6604); pc += 2;
        /* These NOPs should be skipped if the branch is taken */
        write_word(pc, 0x4E71); pc += 2; /* 0x406 */
        write_word(pc, 0x4E71); pc += 2; /* 0x408 */
        /* BRA.s to jump further down (displacement +4 -> target 0x410) */
        write_word(pc, 0x6004); pc += 2; /* 0x40A */
        /* Padding NOPs that should be skipped by BRA */
        write_word(pc, 0x4E71); pc += 2; /* 0x40C */
        write_word(pc, 0x4E71); pc += 2; /* 0x40E */
        /* JMP absolute long to 0x416 */
        write_word(pc, 0x4EF9); pc += 2; /* 0x410 */
        write_long(pc, 0x00000416); pc += 4; /* 0x412 */
        /* STOP to terminate execution */
        write_word(pc, 0x4E72); pc += 2; /* 0x416 */
        write_word(pc, 0x2700); pc += 2;
    }

    void create_duplicate_jsr_program() {
        uint32_t pc = 0x400;

        /* First JSR to 0x500 */
        write_word(pc, 0x4EB9); pc += 2;
        write_long(pc, 0x00000500); pc += 4;

        /* Second JSR to the same subroutine */
        write_word(pc, 0x4EB9); pc += 2;
        write_long(pc, 0x00000500); pc += 4;

        /* STOP to terminate execution */
        write_word(pc, 0x4E72); pc += 2;
        write_word(pc, 0x2700); pc += 2;

        /* Subroutine body: NOP; RTS */
        write_word(0x500, 0x4E71);
        write_word(0x502, 0x4E75);
    }
};

/* ======================================================================== */
/* ===================== BASIC PERFETTO FUNCTIONALITY ==================== */
/* ======================================================================== */

TEST_F(PerfettoTest, InitializationAndCleanup) {
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
}

TEST_F(PerfettoTest, FeatureEnableDisable) {
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
}

TEST_F(PerfettoTest, TraceExportEmpty) {
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
}

/* ======================================================================== */
/* ====================== M68K INTEGRATION TESTS ========================= */
/* ======================================================================== */

TEST_F(PerfettoTest, BasicInstructionTracing) {
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
}

TEST_F(PerfettoTest, FlowTracing) {
    if (::perfetto_init("M68K_Flow_Test") != 0) {
        GTEST_SKIP() << "Perfetto not available, skipping flow tracing test";
    }
    
    /* Enable flow tracing */
    ::perfetto_enable_flow(1);
    
    /* Create a simple program */
    create_simple_program();
    m68k_pulse_reset();
    
    /* Execute some instructions */
    m68k_execute(50);
    
    #ifdef ENABLE_PERFETTO
        /* Verify we can save the trace */
        int save_result = ::m68k_perfetto_save_trace("test_flow.perfetto-trace");
        EXPECT_EQ(save_result, 0);
    #endif
}

TEST_F(PerfettoTest, SymbolNaming) {
    /* Register some symbols */
    ::register_function_name(0x400, "main");
    ::register_function_name(0x500, "subroutine");
    ::register_memory_name(0x1000, "stack_top");
    ::register_memory_range(0x2000, 256, "data_buffer");
    
    /* No crash = success for this basic test */
    SUCCEED();
    
    /* Clean up */
    ::clear_registered_names();
}

TEST_F(PerfettoTest, FlowTracingCapturesJumps) {
    g_flow_events.clear();
    m68k_trace_set_flow_enabled(1);
    m68k_set_trace_flow_callback(CaptureFlowCallback);

    create_flow_program();
    m68k_pulse_reset();

    m68k_execute(200);

    std::set<uint32_t> jump_destinations;
    std::set<uint32_t> jump_sources;
    for (const auto& event : g_flow_events) {
        if (event.type == M68K_TRACE_FLOW_JUMP) {
            jump_sources.insert(event.source_pc);
            jump_destinations.insert(event.dest_pc);
        }
    }

    EXPECT_FALSE(jump_destinations.empty())
        << "No jump flow events captured";

    EXPECT_TRUE(jump_destinations.count(0x40A))
        << "Missing conditional branch jump event";
    EXPECT_TRUE(jump_destinations.count(0x410))
        << "Missing BRA jump event";
    EXPECT_TRUE(jump_destinations.count(0x416))
        << "Missing JMP event";

    EXPECT_TRUE(jump_sources.count(0x404))
        << "Expected BNE at 0x404 to emit a jump event";
    EXPECT_TRUE(jump_sources.count(0x40A))
        << "Expected BRA at 0x40A to emit a jump event";
    EXPECT_TRUE(jump_sources.count(0x410))
        << "Expected JMP at 0x410 to emit a jump event";

    m68k_set_trace_flow_callback(nullptr);
    m68k_trace_set_flow_enabled(0);
}

TEST_F(PerfettoTest, FlowTracingEmitsDuplicateCallEventsForJsrs) {
    g_flow_events.clear();
    m68k_trace_set_flow_enabled(1);
    m68k_set_trace_flow_callback(CaptureFlowCallback);

    create_duplicate_jsr_program();
    m68k_pulse_reset();

    /* Execute until STOP */
    m68k_execute(200);

    std::vector<FlowEvent> call_events;
    for (const auto& event : g_flow_events) {
        if (event.type == M68K_TRACE_FLOW_CALL) {
            call_events.push_back(event);
        }
    }

    std::set<std::pair<uint32_t, uint32_t>> unique_calls;
    for (const auto& event : call_events) {
        unique_calls.emplace(event.source_pc, event.dest_pc);
    }

    EXPECT_EQ(call_events.size(), unique_calls.size())
        << "Duplicate call flow events detected for identical (source,dest) pairs";
    EXPECT_EQ(unique_calls.size(), 2u)
        << "Expected exactly two distinct call events for the back-to-back JSRs";

    m68k_set_trace_flow_callback(nullptr);
    m68k_trace_set_flow_enabled(0);
}

/* ======================================================================== */
/* =================== COMPLEX INSTRUCTION SEQUENCES ==================== */
/* ======================================================================== */

TEST_F(PerfettoTest, BranchAndSubroutineTracing) {
    if (::perfetto_init("M68K_Branch_Test") != 0) {
        GTEST_SKIP() << "Perfetto not available, skipping branch tracing test";
    }
    
    /* Enable all tracing features */
    ::perfetto_enable_flow(1);
    ::perfetto_enable_instructions(1);
    
    /* Create program with branch and subroutine */
    uint32_t pc = 0x400;
    
    /* BRA to 0x410 */
    write_word(pc, 0x600E); pc += 2;  /* BRA.b +14 */
    
    /* Fill with NOPs */
    for (int i = 0; i < 6; i++) {
        write_word(pc, 0x4E71); pc += 2;
    }
    
    /* Target at 0x410: JSR to 0x420 */
    write_word(0x410, 0x4EB9); /* JSR (xxx).L */
    write_long(0x412, 0x00000420);
    
    /* Subroutine at 0x420 */
    write_word(0x420, 0x4E71); /* NOP */
    write_word(0x422, 0x4E75); /* RTS */
    
    m68k_pulse_reset();
    
    /* Execute the sequence */
    for (int i = 0; i < 10; i++) {
        int cycles = m68k_execute(20);
        if (cycles == 0) break;
    }
    
    #ifdef ENABLE_PERFETTO
        /* Save trace for inspection */
        ::m68k_perfetto_save_trace("test_branch_subroutine.perfetto-trace");
    #endif
}

TEST_F(PerfettoTest, MemoryAccessTracing) {
    if (::m68k_perfetto_init("M68K_Memory_Test") != 0) {
        GTEST_SKIP() << "Perfetto not available, skipping memory tracing test";
    }
    
    /* Enable memory tracing */
    ::m68k_perfetto_enable_memory(1);
    
    /* Create program that accesses memory */
    uint32_t pc = 0x400;
    
    /* MOVE.L #$12345678, $2000 */
    write_word(pc, 0x23FC); pc += 2;  /* MOVE.L #imm, (xxx).L */
    write_long(pc, 0x12345678); pc += 4;
    write_long(pc, 0x00002000); pc += 4;
    
    /* MOVE.L $2000, D0 */
    write_word(pc, 0x2039); pc += 2;  /* MOVE.L (xxx).L, D0 */
    write_long(pc, 0x00002000); pc += 4;
    
    /* STOP */
    write_word(pc, 0x4E72); pc += 2;
    write_word(pc, 0x2000); pc += 2;
    
    m68k_pulse_reset();
    
    /* Execute the program */
    m68k_execute(100);
    
    /* Verify the memory operations worked */
    EXPECT_EQ(read_long(0x2000), 0x12345678);
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D0), 0x12345678);
    
    #ifdef ENABLE_PERFETTO
        /* Save trace */
        ::m68k_perfetto_save_trace("test_memory_access.perfetto-trace");
    #endif
}

/* ======================================================================== */
/* ==================== MANUALLY ENCODED TEST PROGRAM ==================== */
/* ======================================================================== */

TEST_F(PerfettoTest, ManuallyEncodedProgram) {
    /* This test uses the manually encoded program data from the original test */
    /* This ensures we can run tests even without vasm assembler */
    
    const unsigned char program_data[] = {
        0x20, 0x3c, 0x00, 0x00, 0x00, 0x05,  /* move.l #5, d0 */
        0x22, 0x3c, 0x00, 0x00, 0x00, 0x03,  /* move.l #3, d1 */
        0xd0, 0x81,                          /* add.l d1, d0 */
        0x61, 0x00, 0x00, 0x06,              /* bsr.w subroutine */
        0x4e, 0x72, 0x27, 0x00,              /* stop #$2700 */
        /* subroutine: */
        0x06, 0x80, 0x00, 0x00, 0x00, 0x02,  /* addi.l #2, d0 */
        0x4e, 0x75                           /* rts */
    };
    
    /* Load the program at 0x400 */
    for (size_t i = 0; i < sizeof(program_data); i++) {
        memory[0x400 + i] = program_data[i];
    }
    
    /* Initialize Perfetto if available */
    if (::m68k_perfetto_init("M68K_Manual_Program") == 0) {
        ::m68k_perfetto_enable_flow(1);
        ::m68k_perfetto_enable_instructions(1);
    }
    
    /* Execute the program */
    m68k_pulse_reset();
    
    int total_cycles = 0;
    for (int i = 0; i < 10; i++) {
        int cycles = m68k_execute(20);
        total_cycles += cycles;
        if (cycles == 0) break;
    }
    
    /* Verify the result */
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D0), 10);  /* 5 + 3 + 2 = 10 */
    
    #ifdef ENABLE_PERFETTO
        /* Save trace if Perfetto was initialized */
        if (::m68k_perfetto_is_initialized()) {
            ::m68k_perfetto_save_trace("test_manual_program.perfetto-trace");
        }
    #endif
}
