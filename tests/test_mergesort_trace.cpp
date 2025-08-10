/* Test that traces merge sort execution using the M68k disassembler */

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include "m68k.h"
#include "m68ktrace.h"

extern "C" {
    void set_read_mem_func(int (*func)(unsigned int address, int size));
    void set_write_mem_func(void (*func)(unsigned int address, int size, unsigned int value));
    void set_pc_hook_func(int (*func)(unsigned int pc));
    
    unsigned int m68k_disassemble(char* str_buff, unsigned int pc, unsigned int cpu_type);
    
    /* Disassembler memory access functions - required by m68k_disassemble */
    unsigned int m68k_read_disassembler_8(unsigned int address);
    unsigned int m68k_read_disassembler_16(unsigned int address);
    unsigned int m68k_read_disassembler_32(unsigned int address);
}

struct InstructionTrace {
    uint32_t pc;
    std::string disassembly;
    uint32_t opcode;
    int cycles;
    std::vector<uint32_t> d_regs;
    std::vector<uint32_t> a_regs;
};

class MergeSortTraceTest : public ::testing::Test {
public:
    static MergeSortTraceTest* instance;
    
    int read_memory(unsigned int address, int size) {
        if (address >= memory.size()) return 0;
        
        switch (size) {
            case 1:
                return memory[address];
            case 2:
                if (address + 1 < memory.size()) {
                    return (memory[address] << 8) | memory[address + 1];
                }
                break;
            case 4:
                if (address + 3 < memory.size()) {
                    return (memory[address] << 24) | 
                           (memory[address + 1] << 16) |
                           (memory[address + 2] << 8) | 
                           memory[address + 3];
                }
                break;
        }
        return 0;
    }

protected:
    std::vector<uint8_t> memory;
    std::vector<InstructionTrace> trace;
    bool enable_tracing = false;
    int total_cycles = 0;
    
    void SetUp() override {
        instance = this;
        memory.resize(1024 * 1024, 0);
        trace.clear();
        enable_tracing = false;
        total_cycles = 0;
        
        /* Set up memory callbacks */
        set_read_mem_func(read_memory_static);
        set_write_mem_func(write_memory_static);
        set_pc_hook_func(pc_hook_static);
        
        /* Initialize M68K */
        m68k_init();
        
        /* Set up reset vector */
        write_long(0, 0x1000);  /* Initial SP */
        write_long(4, 0x400);   /* Initial PC */
        
        /* Reset CPU */
        m68k_pulse_reset();
        
        /* Dummy execution to handle initialization overhead */
        m68k_execute(1);
    }
    
    void TearDown() override {
        instance = nullptr;
    }
    
    bool LoadBinaryFile(const std::string& filename, uint32_t load_address) {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file) {
            return false;
        }
        
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        if (load_address + size > memory.size()) {
            return false;
        }
        
        file.read(reinterpret_cast<char*>(&memory[load_address]), size);
        return file.good();
    }
    
    void write_long(uint32_t addr, uint32_t value) {
        memory[addr] = (value >> 24) & 0xFF;
        memory[addr + 1] = (value >> 16) & 0xFF;
        memory[addr + 2] = (value >> 8) & 0xFF;
        memory[addr + 3] = value & 0xFF;
    }
    
    uint32_t read_long(uint32_t addr) {
        return (memory[addr] << 24) |
               (memory[addr + 1] << 16) |
               (memory[addr + 2] << 8) |
               memory[addr + 3];
    }
    
    uint16_t read_word(uint32_t addr) {
        return (memory[addr] << 8) | memory[addr + 1];
    }
    
    void PrintTrace(int start = -1, int count = -1) {
        if (start < 0) start = 0;
        if (count < 0) count = trace.size();
        
        int end = std::min(start + count, (int)trace.size());
        
        printf("\n=== Instruction Trace ===\n");
        for (int i = start; i < end; i++) {
            const auto& t = trace[i];
            printf("%04d: PC=%06X %-30s", i, t.pc, t.disassembly.c_str());
            
            /* Show register changes for key instructions */
            if (t.disassembly.find("BSR") != std::string::npos ||
                t.disassembly.find("RTS") != std::string::npos ||
                t.disassembly.find("MOVE") != std::string::npos) {
                printf(" D0=%04X D1=%04X D2=%04X A0=%08X SP=%08X",
                       t.d_regs[0] & 0xFFFF, t.d_regs[1] & 0xFFFF, 
                       t.d_regs[2] & 0xFFFF, t.a_regs[0], t.a_regs[7]);
            }
            printf("\n");
        }
    }
    
    void PrintArrayState(const std::string& label) {
        printf("%s: ", label.c_str());
        for (int i = 0; i < 8; i++) {
            printf("%d ", read_word(0x4F4 + i * 2));  /* array at 0x4F4 */
        }
        printf("\n");
    }
    
    std::vector<std::string> FindFunctionCalls() {
        std::vector<std::string> calls;
        for (const auto& t : trace) {
            if (t.disassembly.find("bsr") != std::string::npos) {
                calls.push_back(t.disassembly);
            }
        }
        return calls;
    }
    
    int CountInstructionType(const std::string& pattern) {
        int count = 0;
        for (const auto& t : trace) {
            if (t.disassembly.find(pattern) != std::string::npos) {
                count++;
            }
        }
        return count;
    }
    
    bool VerifyMergeSortBehavior() {
        /* Check for recursive calls pattern - note: disassembler uses lowercase */
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
        
        /* For an 8-element array, merge sort should make recursive calls */
        /* We should see multiple BSR and RTS instructions */
        return bsr_calls >= 7 && rts_returns >= 7;  /* At least 7 calls and returns */
    }
    
private:
    static int read_memory_static(unsigned int address, int size) {
        return instance ? instance->read_memory(address, size) : 0;
    }
    
    static void write_memory_static(unsigned int address, int size, unsigned int value) {
        if (instance) instance->write_memory(address, size, value);
    }
    
    static int pc_hook_static(unsigned int pc) {
        return instance ? instance->pc_hook(pc) : 0;
    }
    
    void write_memory(unsigned int address, int size, unsigned int value) {
        if (address >= memory.size()) return;
        
        switch (size) {
            case 1:
                memory[address] = value & 0xFF;
                break;
            case 2:
                if (address + 1 < memory.size()) {
                    memory[address] = (value >> 8) & 0xFF;
                    memory[address + 1] = value & 0xFF;
                }
                break;
            case 4:
                if (address + 3 < memory.size()) {
                    memory[address] = (value >> 24) & 0xFF;
                    memory[address + 1] = (value >> 16) & 0xFF;
                    memory[address + 2] = (value >> 8) & 0xFF;
                    memory[address + 3] = value & 0xFF;
                }
                break;
        }
    }
    
    int pc_hook(unsigned int pc) {
        if (enable_tracing) {
            InstructionTrace t;
            t.pc = pc;
            t.cycles = total_cycles;
            
            /* Disassemble instruction */
            char disasm_buf[256];
            m68k_disassemble(disasm_buf, pc, M68K_CPU_TYPE_68000);
            t.disassembly = disasm_buf;
            
            /* Read opcode */
            t.opcode = read_word(pc);
            
            /* Capture registers */
            for (int i = 0; i < 8; i++) {
                t.d_regs.push_back(m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + i)));
                t.a_regs.push_back(m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + i)));
            }
            
            trace.push_back(t);
        }
        return 0; /* Continue execution */
    }
};

MergeSortTraceTest* MergeSortTraceTest::instance = nullptr;

/* Disassembler memory access functions */
extern "C" {
    unsigned int m68k_read_disassembler_8(unsigned int address) {
        if (MergeSortTraceTest::instance) {
            return MergeSortTraceTest::instance->read_memory(address, 1);
        }
        return 0;
    }
    
    unsigned int m68k_read_disassembler_16(unsigned int address) {
        if (MergeSortTraceTest::instance) {
            return MergeSortTraceTest::instance->read_memory(address, 2);
        }
        return 0;
    }
    
    unsigned int m68k_read_disassembler_32(unsigned int address) {
        if (MergeSortTraceTest::instance) {
            return MergeSortTraceTest::instance->read_memory(address, 4);
        }
        return 0;
    }
}

TEST_F(MergeSortTraceTest, TraceMergeSortExecution) {
    /* Load the assembled merge sort binary */
    ASSERT_TRUE(LoadBinaryFile("tests/test_mergesort.bin", 0x400));
    
    /* Enable instruction tracing */
    enable_tracing = true;
    m68k_trace_enable(1);
    
    /* Show initial array state */
    PrintArrayState("Initial array");
    
    /* Execute the program */
    printf("\nExecuting merge sort...\n");
    int iterations = 0;
    const int MAX_ITERATIONS = 10000;
    
    while (iterations < MAX_ITERATIONS) {
        int cycles = m68k_execute(10);
        total_cycles += cycles;
        iterations++;
        
        /* Check if stopped or completed */
        if (cycles == 0 || read_word(0x504) == 0xCAFE) {  /* sorted_flag at 0x504 */
            break;
        }
        
        /* Safety check */
        uint32_t current_pc = m68k_get_reg(NULL, M68K_REG_PC);
        if (current_pc < 0x400 || current_pc > 0x600) {
            printf("PC out of range: 0x%04X\n", current_pc);
            break;
        }
    }
    
    printf("Execution complete: %d cycles, %d iterations\n", total_cycles, iterations);
    
    /* Show final array state */
    PrintArrayState("Sorted array");
    
    /* Verify array is sorted */
    uint16_t prev = read_word(0x4F4);
    bool sorted = true;
    for (int i = 1; i < 8; i++) {
        uint16_t curr = read_word(0x4F4 + i * 2);
        if (prev > curr) {
            sorted = false;
            printf("Array not sorted at index %d: %d > %d\n", i, prev, curr);
        }
        prev = curr;
    }
    
    EXPECT_TRUE(sorted) << "Array should be sorted";
    EXPECT_EQ(0xCAFE, read_word(0x504)) << "Sorted flag should be set";
    
    /* Analyze trace */
    printf("\n=== Trace Analysis ===\n");
    EXPECT_TRUE(VerifyMergeSortBehavior()) << "Should show merge sort behavior";
    
    /* Print sample of trace */
    printf("\nFirst 50 instructions:\n");
    PrintTrace(0, 50);
    
    /* Find and print all function calls */
    auto calls = FindFunctionCalls();
    printf("\n=== Function Calls ===\n");
    for (size_t i = 0; i < calls.size(); i++) {
        printf("%zu: %s\n", i, calls[i].c_str());
    }
}

TEST_F(MergeSortTraceTest, AnalyzeMergeSortRecursion) {
    /* Load the assembled merge sort binary */
    ASSERT_TRUE(LoadBinaryFile("tests/test_mergesort.bin", 0x400));
    
    /* Enable instruction tracing */
    enable_tracing = true;
    m68k_trace_enable(1);
    
    /* Execute the program */
    int iterations = 0;
    const int MAX_ITERATIONS = 10000;
    
    while (iterations < MAX_ITERATIONS) {
        int cycles = m68k_execute(10);
        total_cycles += cycles;
        iterations++;
        
        if (cycles == 0 || read_word(0x500) == 0xCAFE) {
            break;
        }
        
        uint32_t current_pc = m68k_get_reg(NULL, M68K_REG_PC);
        if (current_pc < 0x400 || current_pc > 0x600) {
            break;
        }
    }
    
    /* Analyze recursion depth */
    int max_depth = 0;
    int current_depth = 0;
    std::vector<int> call_depths;
    
    for (const auto& t : trace) {
        if (t.disassembly.find("bsr") != std::string::npos) {
            current_depth++;
            call_depths.push_back(current_depth);
            max_depth = std::max(max_depth, current_depth);
        } else if (t.disassembly.find("rts") != std::string::npos) {
            current_depth--;
        }
    }
    
    printf("\n=== Recursion Analysis ===\n");
    printf("Maximum recursion depth: %d\n", max_depth);
    printf("Total function calls: %zu\n", call_depths.size());
    
    /* For merge sort on 8 elements, max depth should be log2(8) = 3 */
    EXPECT_GE(max_depth, 3) << "Recursion depth should be at least log2(n)";
    EXPECT_LE(max_depth, 5) << "Recursion depth should not be excessive";
    
    /* Print call tree */
    printf("\nCall tree visualization:\n");
    current_depth = 0;
    for (const auto& t : trace) {
        if (t.disassembly.find("bsr") != std::string::npos) {
            for (int i = 0; i < current_depth; i++) printf("  ");
            printf("CALL: %s (D0=%d, D1=%d)\n", 
                   t.disassembly.c_str(), 
                   t.d_regs[0] & 0xFFFF, 
                   t.d_regs[1] & 0xFFFF);
            current_depth++;
        } else if (t.disassembly.find("rts") != std::string::npos) {
            current_depth--;
            for (int i = 0; i < current_depth; i++) printf("  ");
            printf("RETURN\n");
        }
    }
}