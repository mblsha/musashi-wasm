/* Refactored vasm binary tests - eliminates ~150 lines of duplication */

#include "m68k_test_common.h"
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
    ASSERT_TRUE(LoadBinaryFile("../tests/test_program.bin", 0x400));
    
    /* Verify the binary was loaded correctly by checking first few instructions */
    EXPECT_EQ(0x303C, read_word(0x400));  /* MOVE.W #5,D0 */
    EXPECT_EQ(0x0005, read_word(0x402));
    EXPECT_EQ(0x611C, read_word(0x404));  /* BSR factorial */
    
    /* Verify data section */
    EXPECT_EQ(0x0008, read_word(0x484));  /* First array element */
    EXPECT_EQ(0x0003, read_word(0x486));  /* Second array element */
}

TEST_F(VasmBinaryTest, ExecuteBinaryWithPerfettoTrace) {
    /* Load the assembled binary */
    ASSERT_TRUE(LoadBinaryFile("../tests/test_program.bin", 0x400));
    
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
        
        /* Safety check */
        uint32_t current_pc = m68k_get_reg(NULL, M68K_REG_PC);
        if (current_pc < 0x400 || current_pc > 0x500) {
            break;
        }
    }
    
    /* Verify execution reached key functions */
    bool found_factorial = false;
    bool found_fibonacci = false;
    bool found_bubble_sort = false;
    
    for (auto pc : pc_hooks) {
        if (pc == 0x41C) found_factorial = true;    /* factorial function */
        if (pc == 0x434) found_fibonacci = true;    /* fibonacci function */
        if (pc == 0x454) found_bubble_sort = true;  /* bubble_sort function */
    }
    
    EXPECT_TRUE(found_factorial) << "Factorial function was not called";
    EXPECT_TRUE(found_fibonacci) << "Fibonacci function was not called";
    EXPECT_TRUE(found_bubble_sort) << "Bubble sort function was not called";
    
    /* Verify results were written to memory */
    uint32_t factorial_result = read_long(0x494);  /* result1 */
    uint32_t fibonacci_result = read_long(0x498);  /* result2 */
    
    EXPECT_EQ(120, factorial_result) << "Factorial(5) should be 120";
    EXPECT_EQ(2, fibonacci_result) << "Fibonacci(3) should be 2";
    
    /* Verify array was sorted */
    uint16_t prev = read_word(0x484);
    for (int i = 1; i < 8; i++) {
        uint16_t curr = read_word(0x484 + i * 2);
        EXPECT_LE(prev, curr) << "Array not sorted at index " << i;
        prev = curr;
    }
    
    /* Save Perfetto trace if initialized */
    if (perfetto_is_initialized()) {
        perfetto_save_trace("vasm_binary_trace.perfetto-trace");
        perfetto_destroy();
    }
}

TEST_F(VasmBinaryTest, ValidateInstructionEncoding) {
    /* Load the assembled binary */
    ASSERT_TRUE(LoadBinaryFile("../tests/test_program.bin", 0x400));
    
    /* Validate specific instruction encodings */
    struct InstructionCheck {
        uint32_t address;
        uint16_t opcode;
        const char* description;
    };
    
    InstructionCheck checks[] = {
        {0x400, 0x303C, "MOVE.W #5,D0"},
        {0x404, 0x611C, "BSR factorial"},
        {0x408, 0x2040, "MOVEA.L D0,A0"},
        {0x40A, 0x303C, "MOVE.W #3,D0"},
        {0x40E, 0x6124, "BSR fibonacci"},
        {0x436, 0x6730, "BEQ .base_case"},
        {0x47C, 0x4E72, "STOP #$2000"}
    };
    
    for (const auto& check : checks) {
        uint16_t actual = read_word(check.address);
        EXPECT_EQ(check.opcode, actual) 
            << "Instruction mismatch at 0x" << std::hex << check.address
            << " for " << check.description
            << " - expected 0x" << check.opcode 
            << " but got 0x" << actual;
    }
}

TEST_F(VasmBinaryTest, ExecuteFactorial) {
    /* Load the assembled binary */
    ASSERT_TRUE(LoadBinaryFile("../tests/test_program.bin", 0x400));
    
    /* Set up to call factorial(5) directly */
    write_long(4, 0x41C);  /* Set PC to factorial function */
    m68k_set_reg(M68K_REG_D0, 5);  /* Parameter = 5 */
    m68k_pulse_reset();
    
    /* Add RTS after factorial to stop execution */
    write_word(0x432, 0x4E75);  /* RTS */
    
    /* Execute factorial */
    pc_hooks.clear();
    m68k_execute(1000);
    
    /* Result should be in D0 */
    EXPECT_EQ(120, m68k_get_reg(NULL, M68K_REG_D0)) << "5! should be 120";
    
    /* Verify recursion happened */
    int factorial_calls = 0;
    for (auto pc : pc_hooks) {
        if (pc == 0x41C) factorial_calls++;
    }
    EXPECT_GT(factorial_calls, 1) << "Factorial should have recursive calls";
}

TEST_F(VasmBinaryTest, ExecuteFibonacci) {
    /* Load the assembled binary */
    ASSERT_TRUE(LoadBinaryFile("../tests/test_program.bin", 0x400));
    
    /* Set up to call fibonacci(5) directly */
    write_long(4, 0x434);  /* Set PC to fibonacci function */
    m68k_set_reg(M68K_REG_D0, 5);  /* Parameter = 5 */
    m68k_pulse_reset();
    
    /* Add RTS after fibonacci to stop execution */
    write_word(0x452, 0x4E75);  /* RTS */
    
    /* Execute fibonacci */
    pc_hooks.clear();
    m68k_execute(2000);
    
    /* Result should be in D0 */
    EXPECT_EQ(5, m68k_get_reg(NULL, M68K_REG_D0)) << "Fib(5) should be 5";
    
    /* Verify recursion happened */
    int fibonacci_calls = 0;
    for (auto pc : pc_hooks) {
        if (pc == 0x434) fibonacci_calls++;
    }
    EXPECT_GT(fibonacci_calls, 1) << "Fibonacci should have recursive calls";
}

TEST_F(VasmBinaryTest, BubbleSortExecution) {
    /* Load the assembled binary */
    ASSERT_TRUE(LoadBinaryFile("../tests/test_program.bin", 0x400));
    
    /* Verify initial array is unsorted */
    uint16_t initial[] = {8, 3, 7, 1, 5, 2, 6, 4};
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(initial[i], read_word(0x484 + i * 2)) 
            << "Initial array mismatch at index " << i;
    }
    
    /* Execute just the bubble sort part */
    write_long(4, 0x454);  /* Set PC to bubble_sort */
    m68k_set_reg(M68K_REG_A0, 0x484);  /* Array address */
    m68k_set_reg(M68K_REG_D0, 8);      /* Array size */
    m68k_pulse_reset();
    
    /* Add STOP after bubble sort */
    write_word(0x47C, 0x4E72);  /* STOP */
    write_word(0x47E, 0x2000);
    
    /* Execute bubble sort */
    m68k_execute(5000);
    
    /* Verify array is sorted */
    uint16_t expected[] = {1, 2, 3, 4, 5, 6, 7, 8};
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(expected[i], read_word(0x484 + i * 2))
            << "Sorted array mismatch at index " << i;
    }
}