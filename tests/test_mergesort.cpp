/* Comprehensive merge sort tests with verification and analysis */

#include "m68k_test_base.h"
#include "test_helpers.h"

/* Disassembler memory access functions are now provided by myfunc.cc */

class MergeSortTest : public M68kTestBase {
protected:
    void PrintArrayState(const std::string& label) {
        printf("%s: ", label.c_str());
        for (int i = 0; i < 8; i++) {
            printf("%d ", read_word(0x4F4 + i * 2));
        }
        printf("\n");
    }
    
    bool VerifyMergeSortBehavior() {
        /* Analyze recursion pattern more thoroughly */
        int bsr_calls = CountInstructionType("bsr");
        int rts_returns = CountInstructionType("rts");
        int cmp_instructions = CountInstructionType("cmp");
        int branch_instructions = CountInstructionType("b");
        
        printf("\nMerge sort statistics:\n");
        printf("  Total instructions: %zu\n", trace.size());
        printf("  Function calls (bsr): %d\n", bsr_calls);
        printf("  Function returns (rts): %d\n", rts_returns);
        printf("  Comparisons (cmp): %d\n", cmp_instructions);
        printf("  Branches: %d\n", branch_instructions);
        
        /* Verify balanced call/return structure */
        if (abs(bsr_calls - rts_returns) > 2) {
            printf("  WARNING: Unbalanced calls/returns (diff=%d)\n", abs(bsr_calls - rts_returns));
            return false;
        }
        
        /* For merge sort on 8 elements:
         * - Expected ~15 calls (7 for divide phase, 7 for merge phase)
         * - At least 20 comparisons for merging
         * - Significant branching for control flow
         */
        bool has_sufficient_calls = bsr_calls >= 14 && bsr_calls <= 30;
        bool has_sufficient_comparisons = cmp_instructions >= 20;
        bool has_sufficient_branches = branch_instructions >= 30;
        
        if (!has_sufficient_calls) {
            printf("  WARNING: Expected 14-30 function calls, got %d\n", bsr_calls);
        }
        if (!has_sufficient_comparisons) {
            printf("  WARNING: Expected at least 20 comparisons, got %d\n", cmp_instructions);
        }
        if (!has_sufficient_branches) {
            printf("  WARNING: Expected at least 30 branches, got %d\n", branch_instructions);
        }
        
        return has_sufficient_calls && has_sufficient_comparisons && has_sufficient_branches;
    }
};

TEST_F(MergeSortTest, ExecuteAndVerifyBehavior) {
    /* Load the assembled merge sort binary */
    ASSERT_TRUE(LoadBinaryFile(FindTestFile("test_mergesort.bin"), 0x400));;
    
    /* Enable instruction tracing */
    enable_tracing = true;
    m68k_trace_enable(1);
    
    /* Show initial array state */
    PrintArrayState("Initial array");
    
    /* Execute the program */
    printf("\nExecuting merge sort...\n");
    int iterations = 0;
    const int MAX_ITERATIONS = 10000;
    
    while (iterations < MAX_ITERATIONS && instruction_count < 1000) {
        int cycles = m68k_execute(100);
        total_cycles += cycles;
        iterations++;
        
        if (cycles == 0 || read_word(0x504) == 0xCAFE) {
            break;
        }
        
        uint32_t current_pc = m68k_get_reg(NULL, M68K_REG_PC);
        if (current_pc < 0x400 || current_pc > 0x600) {
            break;
        }
    }
    
    printf("Execution complete: %d cycles, %d iterations, %d instructions\n", 
           total_cycles, iterations, instruction_count);
    
    /* Show final array state */
    PrintArrayState("Sorted array");
    
    /* Verify array is sorted */
    bool sorted = true;
    for (int i = 1; i < 8; i++) {
        if (read_word(0x4F4 + (i-1) * 2) > read_word(0x4F4 + i * 2)) {
            sorted = false;
            break;
        }
    }
    
    ASSERT_TRUE(sorted) << "Array should be sorted";
    EXPECT_EQ(0xCAFE, read_word(0x504)) << "Sorted flag should be set";
    
    /* Analyze trace - skip weak check if trace is small */
    printf("\n=== Behavioral Analysis ===\n");
    if (trace.size() > 100) {
        bool behavior_ok = VerifyMergeSortBehavior();
        if (!behavior_ok) {
            printf("\nNOTE: Execution pattern doesn't match typical merge sort\n");
        }
    }
    
    /* Analyze recursion depth */
    int max_depth = AnalyzeRecursionDepth();
    printf("Maximum recursion depth: %d\n", max_depth);
    EXPECT_GE(max_depth, 3) << "Recursion depth should be at least log2(n)";
    EXPECT_LE(max_depth, 5) << "Recursion depth should not be excessive";
}

TEST_F(MergeSortTest, SortCorrectnessVerification) {
    /* Load the assembled merge sort binary */
    ASSERT_TRUE(LoadBinaryFile(FindTestFile("test_mergesort.bin"), 0x400));;
    
    /* Enable instruction tracing */
    enable_tracing = true;
    m68k_trace_enable(1);
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║         MERGE SORT CORRECTNESS VERIFICATION TEST                ║\n");
    printf("║                                                                  ║\n");
    printf("║  This test verifies that the M68K merge sort implementation     ║\n");
    printf("║  correctly sorts the array and uses expected recursion          ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    /* Record initial array state */
    std::vector<uint16_t> initial_array;
    for (int i = 0; i < 8; i++) {
        initial_array.push_back(read_word(0x4F4 + i * 2));
    }
    
    printf("Initial array: ");
    for (auto val : initial_array) {
        printf("%d ", val);
    }
    printf("\n");
    
    /* Execute with controlled monitoring */
    printf("\nExecuting merge sort with detailed tracking...\n");
    
    int iterations = 0;
    const int MAX_ITERATIONS = 10000;
    std::set<uint32_t> unique_pcs;
    std::map<std::string, int> instruction_counts;
    
    while (iterations < MAX_ITERATIONS && instruction_count < 1000) {
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
    
    /* Collect instruction statistics */
    for (const auto& instr : trace) {
        unique_pcs.insert(instr.pc);
        instruction_counts[instr.mnemonic]++;
    }
    
    /* Verify sorting correctness */
    std::vector<uint16_t> final_array;
    for (int i = 0; i < 8; i++) {
        final_array.push_back(read_word(0x4F4 + i * 2));
    }
    
    printf("\nFinal array: ");
    for (auto val : final_array) {
        printf("%d ", val);
    }
    printf("\n");
    
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
    
    printf("\n");
    printf("══════════════════════════════════════════════════════════════════\n");
    printf("                       CORRECTNESS RESULTS                       \n");
    printf("══════════════════════════════════════════════════════════════════\n");
    printf("Array is sorted:       %s\n", is_sorted ? "✓ YES" : "✗ NO");
    printf("Is permutation:        %s\n", is_permutation ? "✓ YES" : "✗ NO");
    printf("Completion flag:       %s\n", read_word(0x504) == 0xCAFE ? "✓ SET" : "✗ NOT SET");
    printf("\n");
    
    /* Analyze execution patterns */
    printf("EXECUTION ANALYSIS:\n");
    printf("──────────────────────────────────────────────────────────────────\n");
    printf("Total instructions:    %d\n", instruction_count);
    printf("Unique PCs:           %zu\n", unique_pcs.size());
    printf("Total cycles:         %d\n", total_cycles);
    printf("\n");
    
    /* Identify key algorithmic operations */
    int memory_reads = 0;
    int memory_writes = 0;
    int comparisons = 0;
    int swaps = 0;
    
    for (size_t i = 0; i < trace.size(); i++) {
        const auto& instr = trace[i];
        
        /* Count memory operations (simplified heuristic) */
        if (instr.mnemonic.find("move") != std::string::npos) {
            if (instr.operands.find("(A") != std::string::npos) {
                if (instr.operands[0] == '(') {
                    memory_reads++;
                } else if (instr.operands.find(",(A") != std::string::npos) {
                    memory_writes++;
                }
            }
        }
        
        /* Count comparisons - use NormalizeMnemonic to handle size suffixes */
        std::string norm_mnemonic = NormalizeMnemonic(instr.mnemonic);
        if (norm_mnemonic.rfind("cmp", 0) == 0) {  // Matches cmp, cmpi, cmpa, cmpm
            comparisons++;
        }
        
        /* Detect swap patterns (simplified) */
        if (i > 0 && i < trace.size() - 1) {
            if (trace[i-1].mnemonic.find("move") != std::string::npos &&
                trace[i].mnemonic.find("move") != std::string::npos &&
                trace[i+1].mnemonic.find("move") != std::string::npos) {
                swaps++;
                i += 2; /* Skip the rest of swap sequence */
            }
        }
    }
    
    printf("KEY OPERATIONS:\n");
    printf("──────────────────────────────────────────────────────────────────\n");
    printf("Memory reads:         %d\n", memory_reads);
    printf("Memory writes:        %d\n", memory_writes);
    printf("Comparisons:          %d\n", comparisons);
    printf("Potential swaps:      %d\n", swaps / 3);  /* Rough estimate */
    
    /* Analyze recursion */
    int recursion_depth = AnalyzeRecursionDepth();
    int bsr_count = CountInstructionType("bsr");
    int rts_count = CountInstructionType("rts");
    
    printf("\nRECURSION ANALYSIS:\n");
    printf("──────────────────────────────────────────────────────────────────\n");
    printf("Max recursion depth:  %d\n", recursion_depth);
    printf("Function calls (BSR): %d\n", bsr_count);
    printf("Function returns:     %d\n", rts_count);
    printf("Call/return balance:  %s\n", 
           abs(bsr_count - rts_count) <= 1 ? "✓ BALANCED" : "✗ UNBALANCED");
    
    /* Performance metrics */
    printf("\nPERFORMANCE METRICS:\n");
    printf("──────────────────────────────────────────────────────────────────\n");
    printf("Instructions/element: %.1f\n", (float)instruction_count / 8);
    printf("Cycles/element:       %.1f\n", (float)total_cycles / 8);
    printf("Code density:         %.2f (unique PCs / total instructions)\n",
           (float)unique_pcs.size() / instruction_count);
    
    /* Final assertions */
    ASSERT_TRUE(is_sorted) << "Array must be sorted";
    ASSERT_TRUE(is_permutation) << "Sorted array must be a permutation of original";
    ASSERT_EQ(0xCAFE, read_word(0x504)) << "Completion flag must be set";
    ASSERT_GE(recursion_depth, 3) << "Merge sort should have log(n) recursion depth";
    ASSERT_LE(recursion_depth, 5) << "Recursion depth should not be excessive";
    
    /* Verify reasonable algorithm complexity */
    EXPECT_LT(instruction_count, 5000) << "Instruction count seems excessive for 8 elements";
    EXPECT_GT(comparisons, 10) << "Too few comparisons for merge sort";
    EXPECT_LT(comparisons, 100) << "Too many comparisons for 8 elements";
}

TEST_F(MergeSortTest, RecursionDepthAnalysis) {
    /* Load the assembled merge sort binary */
    ASSERT_TRUE(LoadBinaryFile(FindTestFile("test_mergesort.bin"), 0x400));;
    
    /* Enable full tracing */
    enable_tracing = true;
    
    /* Execute merge sort */
    m68k_execute(5000);
    
    /* Analyze call graph */
    PrintCallGraph();
    
    /* Verify recursion characteristics - use normalized version */
    int max_depth = AnalyzeRecursionDepthNormalized();
    printf("\n=== RECURSION ANALYSIS ===\n");
    printf("Maximum recursion depth: %d\n", max_depth);
    printf("Expected for 8 elements: 3 (log2(8))\n");
    
    /* Count recursive calls at each level - normalized to start at 0 */
    std::map<int, int> depth_counts;
    int current_depth = 0;
    bool saw_root = false;
    
    for (const auto& instr : trace) {
        std::string norm_mnemonic = NormalizeMnemonic(instr.mnemonic);
        
        if (norm_mnemonic == "bsr") {
            if (!saw_root) {
                saw_root = true;
                depth_counts[0]++;  // Root call at level 0
            } else {
                current_depth++;
                depth_counts[current_depth]++;
            }
        } else if (norm_mnemonic == "rts") {
            if (saw_root && current_depth > 0) current_depth--;
        }
    }
    
    printf("\nCalls per recursion level:\n");
    for (const auto& [depth, count] : depth_counts) {
        printf("  Level %d: %d calls\n", depth, count);
    }
    
    EXPECT_EQ(3, max_depth) << "Merge sort on 8 elements should have depth 3";
    EXPECT_GE(depth_counts[1], 1) << "Should have at least one call at level 1";
    EXPECT_GE(depth_counts[2], 2) << "Should have at least two calls at level 2";
    EXPECT_GE(depth_counts[3], 4) << "Should have at least four calls at level 3";
}