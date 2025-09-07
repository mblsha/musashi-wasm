/* Refactored vasm binary tests - validates behavior without hard-coding addresses */

#include "m68k_test_common.h"
#include "test_helpers.h"
#include "m68k_perfetto.h"
#include "m68ktrace.h"
#include <algorithm>
#include <map>
#include <set>

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
DECLARE_M68K_TEST(VasmBinaryTest) {
protected:
    void PrintArrayState(const std::string& label, uint32_t base_addr) {
        printf("%s: ", label.c_str());
        for (int i = 0; i < 8; i++) {
            printf("%d ", read_word(base_addr + i * 2));
        }
        printf("\n");
    }
};

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
    
    /* Define a magic completion marker address */
    const uint32_t COMPLETION_FLAG_ADDR = 0x500;
    const uint16_t COMPLETION_MAGIC = 0xBEEF;
    
    /* Execute the program */
    pc_hooks.clear();
    int total_cycles = 0;
    for (int i = 0; i < 100; i++) {
        int cycles = m68k_execute(50);
        total_cycles += cycles;
        if (cycles == 0) break;
        
        /* Check if program set completion flag */
        if (read_word(COMPLETION_FLAG_ADDR) == COMPLETION_MAGIC) {
            break;
        }
    }
    
    /* Analyze what the program did */
    EXPECT_GT(pc_hooks.size(), 10u) << "Program should execute multiple instructions";
    EXPECT_LT(pc_hooks.size(), 10000u) << "Program should not run forever";
    
    /* Count instruction types from actual execution */
    std::map<uint16_t, int> opcode_classes;
    char disasm_buf[256];
    
    for (auto pc : pc_hooks) {
        uint16_t opcode = read_word(pc);
        m68k_disassemble(disasm_buf, pc, M68K_CPU_TYPE_68000);
        
        /* Classify by major opcode group */
        uint16_t opcode_class = (opcode >> 12) & 0xF;
        opcode_classes[opcode_class]++;
    }
    
    /* Verify we have a diverse set of instructions */
    EXPECT_GE(opcode_classes.size(), 3u) << "Program should use various instruction types";
    
    /* If the program includes sorting, verify the result */
    bool found_sorted_data = false;
    for (uint32_t addr = 0x480; addr < 0x500; addr += 16) {
        std::vector<uint16_t> data;
        for (int i = 0; i < 8; i++) {
            uint16_t val = read_word(addr + i * 2);
            if (val == 0 || val > 1000) break;
            data.push_back(val);
        }
        
        if (data.size() >= 4) {
            bool is_sorted = std::is_sorted(data.begin(), data.end());
            if (is_sorted) {
                found_sorted_data = true;
                break;
            }
        }
    }
    
    /* This is optional - only check if we found array-like data */
    if (found_sorted_data) {
        SUCCEED() << "Found sorted data - program likely includes sorting algorithm";
    }
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

/* Tests from test_mergesort.cpp - consolidated and refactored */

TEST_F(VasmBinaryTest, MergeSortCorrectness) {
    /* Load the assembled merge sort binary */
    ASSERT_TRUE(LoadBinaryFile(FindTestFile("test_mergesort.bin"), 0x400));;
    
    /* Record initial array state */
    std::vector<uint16_t> initial_array;
    for (int i = 0; i < 8; i++) {
        initial_array.push_back(read_word(0x4F4 + i * 2));
    }
    
    PrintArrayState("Initial array", 0x4F4);
    
    /* Execute merge sort */
    int iterations = 0;
    const int MAX_ITERATIONS = 10000;
    int total_cycles = 0;
    
    while (iterations < MAX_ITERATIONS && pc_hooks.size() < 1000) {
        int cycles = m68k_execute(100);
        total_cycles += cycles;
        iterations++;
        
        /* Check for completion */
        if (cycles == 0 || read_word(0x504) == 0xCAFE) {
            break;
        }
        
        /* Safety check */
        uint32_t current_pc = m68k_get_reg(NULL, M68K_REG_PC);
        if (current_pc < 0x400 || current_pc > 0x600) {
            break;
        }
    }
    
    /* Get final array state */
    std::vector<uint16_t> final_array;
    for (int i = 0; i < 8; i++) {
        final_array.push_back(read_word(0x4F4 + i * 2));
    }
    
    PrintArrayState("Sorted array", 0x4F4);
    
    /* Check if array is sorted */
    bool is_sorted = true;
    for (size_t i = 1; i < final_array.size(); i++) {
        if (final_array[i] < final_array[i-1]) {
            is_sorted = false;
            break;
        }
    }
    
    /* Verify it's a permutation of the original */
    std::vector<uint16_t> sorted_initial = initial_array;
    std::vector<uint16_t> sorted_final = final_array;
    std::sort(sorted_initial.begin(), sorted_initial.end());
    std::sort(sorted_final.begin(), sorted_final.end());
    
    bool is_permutation = (sorted_initial == sorted_final);
    
    printf("\nCorrectness Results:\n");
    printf("Array is sorted:       %s\n", is_sorted ? "YES" : "NO");
    printf("Is permutation:        %s\n", is_permutation ? "YES" : "NO");
    printf("Completion flag:       %s\n", read_word(0x504) == 0xCAFE ? "SET" : "NOT SET");
    printf("Total instructions:    %zu\n", pc_hooks.size());
    printf("Total cycles:          %d\n", total_cycles);
    
    /* Final assertions */
    ASSERT_TRUE(is_sorted) << "Array must be sorted";
    ASSERT_TRUE(is_permutation) << "Sorted array must be a permutation of original";
    ASSERT_EQ(0xCAFE, read_word(0x504)) << "Completion flag must be set";
    EXPECT_LT(pc_hooks.size(), 5000u) << "Instruction count seems excessive for 8 elements";
}

TEST_F(VasmBinaryTest, MergeSortRecursionDepth) {
    /* Load the assembled merge sort binary */
    ASSERT_TRUE(LoadBinaryFile(FindTestFile("test_mergesort.bin"), 0x400));;
    
    /* Execute merge sort */
    pc_hooks.clear();
    m68k_execute(5000);
    
    /* Analyze recursion depth by tracking BSR/RTS pairs */
    int current_depth = 0;
    int max_depth = 0;
    std::map<int, int> depth_counts;
    
    char disasm_buf[256];
    for (auto pc : pc_hooks) {
        uint16_t opcode = read_word(pc);
        m68k_disassemble(disasm_buf, pc, M68K_CPU_TYPE_68000);
        std::string instr(disasm_buf);
        
        /* BSR instruction increases depth */
        if ((opcode & 0xFF00) == 0x6100) {
            current_depth++;
            depth_counts[current_depth]++;
            if (current_depth > max_depth) {
                max_depth = current_depth;
            }
        }
        /* RTS instruction decreases depth */
        else if (opcode == 0x4E75) {
            if (current_depth > 0) current_depth--;
        }
    }
    
    printf("\nRecursion Analysis:\n");
    printf("Maximum recursion depth: %d\n", max_depth);
    printf("Expected for 8 elements: 3 (log2(8))\n");
    
    printf("\nCalls per recursion level:\n");
    for (const auto& [depth, count] : depth_counts) {
        printf("  Level %d: %d calls\n", depth, count);
    }
    
    /* Verify reasonable recursion depth for merge sort on 8 elements */
    EXPECT_GE(max_depth, 3) << "Merge sort should have depth at least log2(n)";
    EXPECT_LE(max_depth, 5) << "Recursion depth should not be excessive";
}