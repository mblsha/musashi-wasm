/* Refactored vasm binary tests - validates behavior without hard-coding addresses */

#include "m68k_test_common.h"
#include "test_helpers.h"
#include "m68k_perfetto.h"
#include "m68ktrace.h"

extern "C" {
    int perfetto_init(const char* process_name);
    void perfetto_destroy(void);
    void perfetto_enable_flow(int enable);
    void perfetto_enable_memory(int enable);
    void perfetto_enable_instructions(int enable);
    int perfetto_save_trace(const char* filename);
    int perfetto_is_initialized(void);
}

/* Simple test class using minimal base */
DECLARE_M68K_TEST(VasmBinaryTest) {};

TEST_F(VasmBinaryTest, LoadAndValidateBinary) {
    /* Load the assembled binary */
    ASSERT_TRUE(LoadBinaryFile(FindTestFile("test_program.bin"), 0x400));
    
    /* Just verify the binary loaded successfully - don't check specific opcodes */
    /* The binary should start with executable code */
    uint16_t first_word = read_word(0x400);
    EXPECT_NE(0x0000, first_word) << "Binary should contain code at start";
    EXPECT_NE(0xFFFF, first_word) << "Binary should contain valid code";
}

TEST_F(VasmBinaryTest, ExecuteBinaryWithPerfettoTrace) {
    /* Load the assembled binary */
    ASSERT_TRUE(LoadBinaryFile(FindTestFile("test_program.bin"), 0x400));;
    
    /* Enable M68K tracing */
    m68k_trace_enable(1);
    
    /* Initialize Perfetto */
    if (perfetto_init("VasmBinary") == 0) {
        perfetto_enable_flow(1);
        perfetto_enable_memory(0);      /* Disable memory tracing for cleaner output */
        perfetto_enable_instructions(0); /* Disable instruction tracing */
    }
    
    /* Execute the program */
    int total_cycles = 0;
    int iterations = 0;
    const int MAX_ITERATIONS = 1000;
    
    while (iterations < MAX_ITERATIONS) {
        int cycles = m68k_execute(100);
        total_cycles += cycles;
        iterations++;
        
        /* Check if stopped */
        if (cycles == 0) {
            break;
        }
        
        /* Safety check - ensure PC stays in reasonable range */
        uint32_t current_pc = m68k_get_reg(NULL, M68K_REG_PC);
        if (current_pc < 0x400 || current_pc > 0x600) {
            break;
        }
    }
    
    /* Verify program ran for a reasonable number of cycles */
    EXPECT_GT(total_cycles, 100) << "Program should execute multiple instructions";
    EXPECT_LT(iterations, MAX_ITERATIONS) << "Program should terminate normally";
    
    /* Verify execution included subroutine calls (BSR/JSR) and returns (RTS) */
    int subroutine_calls = 0;
    int subroutine_returns = 0;
    
    char disasm_buf[256];
    for (auto pc : pc_hooks) {
        uint16_t opcode = read_word(pc);
        m68k_disassemble(disasm_buf, pc, M68K_CPU_TYPE_68000);
        
        /* Check for subroutine call patterns */
        if ((opcode & 0xFF00) == 0x6100 || /* BSR */
            (opcode & 0xFFC0) == 0x4E80) {  /* JSR */
            subroutine_calls++;
        }
        /* Check for RTS */
        if (opcode == 0x4E75) {
            subroutine_returns++;
        }
    }
    
    EXPECT_GT(subroutine_calls, 0) << "Program should contain subroutine calls";
    EXPECT_GT(subroutine_returns, 0) << "Program should contain subroutine returns";
    
    /* Verify results area was modified (assuming results stored after code) */
    bool memory_modified = false;
    for (uint32_t addr = 0x490; addr < 0x4A0; addr += 4) {
        if (read_long(addr) != 0) {
            memory_modified = true;
            break;
        }
    }
    EXPECT_TRUE(memory_modified) << "Program should write results to memory";
    
    /* Save Perfetto trace if initialized */
    if (perfetto_is_initialized()) {
        perfetto_save_trace("vasm_binary_trace.perfetto-trace");
        perfetto_destroy();
    }
}

TEST_F(VasmBinaryTest, ValidateProgramStructure) {
    /* Load the assembled binary */
    ASSERT_TRUE(LoadBinaryFile(FindTestFile("test_program.bin"), 0x400));;
    
    /* Scan for common M68K instruction patterns without hard-coding addresses */
    int move_instructions = 0;
    int branch_instructions = 0;
    int compare_instructions = 0;
    int arithmetic_instructions = 0;
    
    /* Scan reasonable code area */
    for (uint32_t addr = 0x400; addr < 0x480; addr += 2) {
        uint16_t opcode = read_word(addr);
        
        /* MOVE variants (bits 15-14 = 00) */
        if ((opcode >> 14) == 0) {
            move_instructions++;
        }
        /* Branch instructions (0x6xxx) */
        else if ((opcode >> 12) == 0x6) {
            branch_instructions++;
        }
        /* CMP instructions (0xBxxx or 0x0Cxx) */
        else if ((opcode >> 12) == 0xB || (opcode >> 8) == 0x0C) {
            compare_instructions++;
        }
        /* ADD/SUB (0xDxxx/0x9xxx) */
        else if ((opcode >> 12) == 0xD || (opcode >> 12) == 0x9) {
            arithmetic_instructions++;
        }
    }
    
    /* A real program should have a mix of instruction types */
    EXPECT_GT(move_instructions, 2) << "Program should contain MOVE instructions";
    EXPECT_GT(branch_instructions, 1) << "Program should contain branches";
    EXPECT_GT(compare_instructions, 0) << "Program should contain comparisons";
    EXPECT_GT(arithmetic_instructions, 0) << "Program should contain arithmetic";
}

TEST_F(VasmBinaryTest, ExecuteWithRecursionDetection) {
    /* Load the assembled binary */
    ASSERT_TRUE(LoadBinaryFile(FindTestFile("test_program.bin"), 0x400));;
    
    /* Execute program and track call depth */
    pc_hooks.clear();
    m68k_execute(5000);
    
    /* Analyze execution for recursion patterns */
    std::map<uint32_t, int> function_entry_counts;
    uint32_t last_pc = 0;
    
    for (auto pc : pc_hooks) {
        uint16_t opcode = read_word(last_pc);
        
        /* If last instruction was BSR/JSR, current PC is a function entry */
        if (last_pc != 0 && 
            ((opcode & 0xFF00) == 0x6100 || /* BSR */
             (opcode & 0xFFC0) == 0x4E80)) { /* JSR */
            function_entry_counts[pc]++;
        }
        last_pc = pc;
    }
    
    /* Check if any function was called multiple times (likely recursion) */
    bool has_recursion = false;
    for (const auto& entry : function_entry_counts) {
        if (entry.second > 1) {
            has_recursion = true;
            break;
        }
    }
    
    EXPECT_TRUE(has_recursion) << "Test program should demonstrate recursion";
}

TEST_F(VasmBinaryTest, VerifyDataSorting) {
    /* Load the assembled binary */
    ASSERT_TRUE(LoadBinaryFile(FindTestFile("test_program.bin"), 0x400));;
    
    /* Find data section by looking for non-instruction patterns */
    uint32_t data_start = 0;
    for (uint32_t addr = 0x480; addr < 0x500; addr += 2) {
        uint16_t word = read_word(addr);
        /* Look for small positive values that could be array data */
        if (word > 0 && word < 100) {
            data_start = addr;
            break;
        }
    }
    
    if (data_start > 0) {
        /* Capture initial array values */
        std::vector<uint16_t> initial_values;
        for (int i = 0; i < 8; i++) {
            uint16_t val = read_word(data_start + i * 2);
            if (val == 0 || val > 1000) break; /* Stop if we hit non-data */
            initial_values.push_back(val);
        }
        
        if (initial_values.size() >= 4) {
            /* Execute the program */
            m68k_execute(5000);
            
            /* Check if the same memory area now contains sorted values */
            std::vector<uint16_t> final_values;
            for (size_t i = 0; i < initial_values.size(); i++) {
                final_values.push_back(read_word(data_start + i * 2));
            }
            
            /* Verify values are sorted */
            bool is_sorted = true;
            for (size_t i = 1; i < final_values.size(); i++) {
                if (final_values[i] < final_values[i-1]) {
                    is_sorted = false;
                    break;
                }
            }
            
            EXPECT_TRUE(is_sorted) << "Array should be sorted after execution";
            
            /* Verify it's a permutation of original values */
            std::sort(initial_values.begin(), initial_values.end());
            std::sort(final_values.begin(), final_values.end());
            EXPECT_EQ(initial_values, final_values) << "Sorted array should be permutation of original";
        }
    }
}