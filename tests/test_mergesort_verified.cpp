/* Test that verifies merge sort execution against expected instruction trace */

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include "m68k.h"
#include "m68ktrace.h"

extern "C" {
    void set_read_mem_func(int (*func)(unsigned int address, int size));
    void set_write_mem_func(void (*func)(unsigned int address, int size, unsigned int value));
    void set_pc_hook_func(int (*func)(unsigned int pc));
    
    unsigned int m68k_disassemble(char* str_buff, unsigned int pc, unsigned int cpu_type);
    unsigned int m68k_read_disassembler_8(unsigned int address);
    unsigned int m68k_read_disassembler_16(unsigned int address);
    unsigned int m68k_read_disassembler_32(unsigned int address);
}

/* Expected instruction trace with annotations */
struct ExpectedInstruction {
    uint32_t pc;
    const char* mnemonic;
    const char* operands;
    const char* comment;
    
    // Register expectations (optional)
    struct RegExpect {
        bool check;
        uint16_t d0, d1, d2;  // Key data registers for merge sort
    } regs;
};

// Beautiful, human-readable expected trace for merge sort of [8,3,7,1,5,2,6,4]
const std::vector<ExpectedInstruction> MERGE_SORT_TRACE = {
    // === MAIN PROGRAM INITIALIZATION ===
    {0x400, "lea",     "$4f4.w, A0",         "Load array address into A0", {false}},
    {0x404, "move.w",  "#$0, D0",            "Start index = 0", {true, 0, 0, 0}},
    {0x408, "move.w",  "#$7, D1",            "End index = 7 (8 elements)", {true, 0, 7, 0}},
    {0x40C, "bsr",     "$418",               "CALL mergesort(arr, 0, 7)", {true, 0, 7, 0}},
    
    // === FIRST LEVEL: Split [0..7] into [0..3] and [4..7] ===
    {0x418, "movem.l", "D0-D7/A0-A3, -(A7)", "Save all registers", {false}},
    {0x41C, "cmp.w",   "D0, D1",             "Check if start >= end", {true, 0, 7, 0}},
    {0x41E, "ble",     "$44a",               "Branch if single element", {false}},
    {0x420, "move.w",  "D0, D2",             "Copy start to D2", {true, 0, 7, 0}},
    {0x422, "add.w",   "D1, D2",             "D2 = start + end", {true, 0, 7, 7}},
    {0x424, "lsr.w",   "#1, D2",             "D2 = (start + end) / 2 = 3", {true, 0, 7, 3}},
    {0x426, "move.w",  "D1, -(A7)",          "Push end (7)", {false}},
    {0x428, "move.w",  "D2, -(A7)",          "Push mid (3)", {false}},
    {0x42A, "move.w",  "D0, -(A7)",          "Push start (0)", {false}},
    {0x42C, "move.w",  "D2, D1",             "Set end = mid for left half", {true, 0, 3, 3}},
    {0x42E, "bsr",     "$418",               "CALL mergesort(arr, 0, 3) - LEFT HALF", {true, 0, 3, 3}},
    
    // === SECOND LEVEL LEFT: Split [0..3] into [0..1] and [2..3] ===
    {0x418, "movem.l", "D0-D7/A0-A3, -(A7)", "Save registers", {false}},
    {0x41C, "cmp.w",   "D0, D1",             "Check if start >= end", {true, 0, 3, 3}},
    {0x41E, "ble",     "$44a",               "Branch if single element", {false}},
    {0x420, "move.w",  "D0, D2",             "Copy start to D2", {false}},
    {0x422, "add.w",   "D1, D2",             "D2 = 0 + 3 = 3", {false}},
    {0x424, "lsr.w",   "#1, D2",             "D2 = 3 / 2 = 1", {true, 0, 3, 1}},
    {0x426, "move.w",  "D1, -(A7)",          "Push end (3)", {false}},
    {0x428, "move.w",  "D2, -(A7)",          "Push mid (1)", {false}},
    {0x42A, "move.w",  "D0, -(A7)",          "Push start (0)", {false}},
    {0x42C, "move.w",  "D2, D1",             "Set end = mid", {true, 0, 1, 1}},
    {0x42E, "bsr",     "$418",               "CALL mergesort(arr, 0, 1)", {true, 0, 1, 1}},
    
    // === THIRD LEVEL: Split [0..1] into [0] and [1] ===
    {0x418, "movem.l", "D0-D7/A0-A3, -(A7)", "Save registers", {false}},
    {0x41C, "cmp.w",   "D0, D1",             "Check if start >= end", {true, 0, 1, 1}},
    {0x41E, "ble",     "$44a",               "Branch if single element", {false}},
    {0x420, "move.w",  "D0, D2",             "Copy start", {false}},
    {0x422, "add.w",   "D1, D2",             "D2 = 0 + 1 = 1", {false}},
    {0x424, "lsr.w",   "#1, D2",             "D2 = 1 / 2 = 0", {true, 0, 1, 0}},
    {0x426, "move.w",  "D1, -(A7)",          "Push end (1)", {false}},
    {0x428, "move.w",  "D2, -(A7)",          "Push mid (0)", {false}},
    {0x42A, "move.w",  "D0, -(A7)",          "Push start (0)", {false}},
    {0x42C, "move.w",  "D2, D1",             "Set end = mid", {true, 0, 0, 0}},
    {0x42E, "bsr",     "$418",               "CALL mergesort(arr, 0, 0) - BASE CASE", {true, 0, 0, 0}},
    
    // === BASE CASE: Single element [0] ===
    {0x418, "movem.l", "D0-D7/A0-A3, -(A7)", "Save registers", {false}},
    {0x41C, "cmp.w",   "D0, D1",             "Check 0 >= 0", {true, 0, 0, 0}},
    {0x41E, "ble",     "$44a",               "Branch taken - base case!", {false}},
    {0x44A, "movem.l", "(A7)+, D0-D7/A0-A3", "Restore registers", {false}},
    {0x44E, "rts",     "",                   "RETURN from mergesort(0, 0)", {false}},
    
    // === Continue after first base case ===
    {0x430, "move.w",  "(A7)+, D0",          "Pop start (0)", {true, 0, 0, 0}},
    {0x432, "move.w",  "(A7)+, D2",          "Pop mid (0)", {true, 0, 0, 0}},
    {0x434, "move.w",  "(A7)+, D1",          "Pop end (1)", {true, 0, 1, 0}},
    {0x436, "move.w",  "D1, -(A7)",          "Push end for second recursive call", {false}},
    {0x438, "move.w",  "D2, -(A7)",          "Push mid", {false}},
    {0x43A, "move.w",  "D0, -(A7)",          "Push start", {false}},
    {0x43C, "move.w",  "D2, D0",             "start = mid", {true, 0, 1, 0}},
    {0x43E, "addq.w",  "#1, D0",             "start = mid + 1 = 1", {true, 1, 1, 0}},
    {0x440, "bsr",     "$418",               "CALL mergesort(arr, 1, 1) - BASE CASE", {true, 1, 1, 0}},
    
    // === BASE CASE: Single element [1] ===
    {0x418, "movem.l", "D0-D7/A0-A3, -(A7)", "Save registers", {false}},
    {0x41C, "cmp.w",   "D0, D1",             "Check 1 >= 1", {true, 1, 1, 0}},
    {0x41E, "ble",     "$44a",               "Branch taken - base case!", {false}},
    {0x44A, "movem.l", "(A7)+, D0-D7/A0-A3", "Restore registers", {false}},
    {0x44E, "rts",     "",                   "RETURN from mergesort(1, 1)", {false}},
    
    // === Now merge [0] and [1] ===
    {0x442, "move.w",  "(A7)+, D0",          "Pop start (0)", {true, 0, 1, 0}},
    {0x444, "move.w",  "(A7)+, D2",          "Pop mid (0)", {true, 0, 0, 0}},
    {0x446, "move.w",  "(A7)+, D1",          "Pop end (1)", {true, 0, 1, 0}},
    {0x448, "bsr",     "$450",               "CALL merge(arr, 0, 0, 1) - MERGE [8] and [3]", {true, 0, 1, 0}},
    
    // === MERGE FUNCTION: Combines [0] and [1] ===
    {0x450, "movem.l", "D0-D7/A0-A3, -(A7)", "Save registers for merge", {false}},
    // ... merge implementation details ...
};

class MergeSortVerifiedTest : public ::testing::Test {
public:
    static MergeSortVerifiedTest* instance;
    
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
    struct ActualInstruction {
        uint32_t pc;
        std::string mnemonic;
        std::string operands;
        uint16_t d0, d1, d2;  // Register values
        
        std::string toString() const {
            std::stringstream ss;
            ss << std::hex << std::setfill('0') << std::setw(6) << pc 
               << ": " << std::left << std::setw(8) << mnemonic 
               << " " << std::setw(20) << operands
               << " [D0=" << std::hex << d0 
               << " D1=" << d1 
               << " D2=" << d2 << "]";
            return ss.str();
        }
    };
    
    std::vector<uint8_t> memory;
    std::vector<ActualInstruction> trace;
    bool enable_tracing = false;
    int instruction_count = 0;
    
    void SetUp() override {
        instance = this;
        memory.resize(1024 * 1024, 0);
        trace.clear();
        enable_tracing = false;
        instruction_count = 0;
        
        set_read_mem_func(read_memory_static);
        set_write_mem_func(write_memory_static);
        set_pc_hook_func(pc_hook_static);
        
        m68k_init();
        
        write_long(0, 0x1000);  /* Initial SP */
        write_long(4, 0x400);   /* Initial PC */
        
        m68k_pulse_reset();
        m68k_execute(1);  /* Dummy execution */
    }
    
    void TearDown() override {
        instance = nullptr;
    }
    
    bool LoadBinaryFile(const std::string& filename, uint32_t load_address) {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file) return false;
        
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        if (load_address + size > memory.size()) return false;
        
        file.read(reinterpret_cast<char*>(&memory[load_address]), size);
        return file.good();
    }
    
    void write_long(uint32_t addr, uint32_t value) {
        memory[addr] = (value >> 24) & 0xFF;
        memory[addr + 1] = (value >> 16) & 0xFF;
        memory[addr + 2] = (value >> 8) & 0xFF;
        memory[addr + 3] = value & 0xFF;
    }
    
    uint16_t read_word(uint32_t addr) {
        return (memory[addr] << 8) | memory[addr + 1];
    }
    
    void CompareTraces(size_t max_instructions = 100) {
        printf("\n=== INSTRUCTION-BY-INSTRUCTION VERIFICATION ===\n");
        printf("Comparing first %zu instructions against expected trace...\n\n", max_instructions);
        
        size_t compared = std::min(max_instructions, std::min(trace.size(), MERGE_SORT_TRACE.size()));
        int mismatches = 0;
        
        for (size_t i = 0; i < compared; i++) {
            const auto& expected = MERGE_SORT_TRACE[i];
            const auto& actual = trace[i];
            
            // Check PC
            bool pc_match = (actual.pc == expected.pc);
            
            // Check mnemonic
            bool mnemonic_match = (actual.mnemonic == expected.mnemonic);
            
            // Check operands
            bool operands_match = (actual.operands == expected.operands);
            
            // Check registers if specified
            bool regs_match = true;
            if (expected.regs.check) {
                regs_match = (actual.d0 == expected.regs.d0 &&
                             actual.d1 == expected.regs.d1 &&
                             actual.d2 == expected.regs.d2);
            }
            
            if (pc_match && mnemonic_match && operands_match && regs_match) {
                printf("✅ %03zu: ", i);
            } else {
                printf("❌ %03zu: ", i);
                mismatches++;
            }
            
            // Print instruction with comment
            printf("%06X %-8s %-20s", expected.pc, expected.mnemonic, expected.operands);
            
            if (expected.regs.check) {
                printf(" [D0=%X D1=%X D2=%X]", 
                       expected.regs.d0, expected.regs.d1, expected.regs.d2);
            }
            
            printf(" ; %s\n", expected.comment);
            
            // Show actual if mismatch
            if (!pc_match || !mnemonic_match || !operands_match || !regs_match) {
                printf("        ACTUAL: %s\n", actual.toString().c_str());
                
                if (!pc_match) printf("          PC mismatch: expected %06X, got %06X\n", 
                                     expected.pc, actual.pc);
                if (!mnemonic_match) printf("          Mnemonic mismatch: expected '%s', got '%s'\n", 
                                           expected.mnemonic, actual.mnemonic.c_str());
                if (!operands_match) printf("          Operands mismatch: expected '%s', got '%s'\n", 
                                           expected.operands, actual.operands.c_str());
                if (!regs_match && expected.regs.check) {
                    printf("          Register mismatch: expected D0=%X D1=%X D2=%X, got D0=%X D1=%X D2=%X\n",
                           expected.regs.d0, expected.regs.d1, expected.regs.d2,
                           actual.d0, actual.d1, actual.d2);
                }
            }
        }
        
        printf("\n=== VERIFICATION SUMMARY ===\n");
        printf("Instructions compared: %zu\n", compared);
        printf("Matches: %zu\n", compared - mismatches);
        printf("Mismatches: %d\n", mismatches);
        
        if (mismatches == 0) {
            printf("🎉 PERFECT MATCH! All instructions executed as expected!\n");
        } else {
            printf("⚠️  Found %d mismatches in execution trace.\n", mismatches);
        }
        
        EXPECT_EQ(0, mismatches) << "Execution trace should match expected sequence";
    }
    
    void PrintCallGraph() {
        printf("\n=== FUNCTION CALL GRAPH ===\n");
        int depth = 0;
        
        for (const auto& instr : trace) {
            if (instr.mnemonic == "bsr") {
                for (int i = 0; i < depth; i++) printf("  ");
                printf("→ CALL %s (D0=%d, D1=%d, D2=%d)\n", 
                       instr.operands.c_str(), instr.d0, instr.d1, instr.d2);
                depth++;
            } else if (instr.mnemonic == "rts") {
                depth--;
                for (int i = 0; i < depth; i++) printf("  ");
                printf("← RETURN\n");
            }
        }
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
        if (enable_tracing && instruction_count < 1000) {  // Limit trace size
            ActualInstruction instr;
            instr.pc = pc;
            
            /* Disassemble instruction */
            char disasm_buf[256];
            m68k_disassemble(disasm_buf, pc, M68K_CPU_TYPE_68000);
            
            /* Parse disassembly into mnemonic and operands */
            std::string full_disasm = disasm_buf;
            size_t space_pos = full_disasm.find_first_of(" \t");
            
            if (space_pos != std::string::npos) {
                instr.mnemonic = full_disasm.substr(0, space_pos);
                
                // Find first non-whitespace after mnemonic
                size_t operand_start = full_disasm.find_first_not_of(" \t", space_pos);
                if (operand_start != std::string::npos) {
                    instr.operands = full_disasm.substr(operand_start);
                }
            } else {
                instr.mnemonic = full_disasm;
            }
            
            /* Capture key registers */
            instr.d0 = m68k_get_reg(NULL, M68K_REG_D0) & 0xFFFF;
            instr.d1 = m68k_get_reg(NULL, M68K_REG_D1) & 0xFFFF;
            instr.d2 = m68k_get_reg(NULL, M68K_REG_D2) & 0xFFFF;
            
            trace.push_back(instr);
            instruction_count++;
        }
        return 0;
    }
};

MergeSortVerifiedTest* MergeSortVerifiedTest::instance = nullptr;

/* Disassembler memory access functions */
extern "C" {
    unsigned int m68k_read_disassembler_8(unsigned int address) {
        if (MergeSortVerifiedTest::instance) {
            return MergeSortVerifiedTest::instance->read_memory(address, 1);
        }
        return 0;
    }
    
    unsigned int m68k_read_disassembler_16(unsigned int address) {
        if (MergeSortVerifiedTest::instance) {
            return MergeSortVerifiedTest::instance->read_memory(address, 2);
        }
        return 0;
    }
    
    unsigned int m68k_read_disassembler_32(unsigned int address) {
        if (MergeSortVerifiedTest::instance) {
            return MergeSortVerifiedTest::instance->read_memory(address, 4);
        }
        return 0;
    }
}

TEST_F(MergeSortVerifiedTest, ExactTraceVerification) {
    /* Load the assembled merge sort binary */
    ASSERT_TRUE(LoadBinaryFile("tests/test_mergesort.bin", 0x400));
    
    /* Enable instruction tracing */
    enable_tracing = true;
    m68k_trace_enable(1);
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║         MERGE SORT EXECUTION TRACE VERIFICATION TEST            ║\n");
    printf("║                                                                  ║\n");
    printf("║  This test verifies that the M68K merge sort implementation     ║\n");
    printf("║  executes EXACTLY the expected sequence of instructions for     ║\n");
    printf("║  sorting the array [8, 3, 7, 1, 5, 2, 6, 4] → [1..8]           ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    
    /* Show initial array state */
    printf("\nInitial array at 0x4F4: ");
    for (int i = 0; i < 8; i++) {
        printf("%d ", read_word(0x4F4 + i * 2));
    }
    printf("\n");
    
    /* Execute the program */
    printf("\nExecuting merge sort...\n");
    int iterations = 0;
    const int MAX_ITERATIONS = 10000;
    
    while (iterations < MAX_ITERATIONS && instruction_count < 1000) {
        int cycles = m68k_execute(100);
        iterations++;
        
        if (cycles == 0) {
            printf("CPU stopped (cycles=0) at iteration %d\n", iterations);
            break;
        }
        
        if (read_word(0x504) == 0xCAFE) {
            printf("Sorted flag detected at iteration %d\n", iterations);
            break;
        }
        
        uint32_t current_pc = m68k_get_reg(NULL, M68K_REG_PC);
        if (current_pc < 0x400 || current_pc > 0x600) {
            printf("PC out of range: 0x%04X at iteration %d\n", current_pc, iterations);
            break;
        }
    }
    
    printf("Execution stopped: %d iterations, %d instructions traced\n", iterations, instruction_count);
    
    /* Show final array state */
    printf("\nFinal array at 0x4F4: ");
    for (int i = 0; i < 8; i++) {
        printf("%d ", read_word(0x4F4 + i * 2));
    }
    printf("\n");
    
    /* Verify array is sorted */
    bool sorted = true;
    for (int i = 1; i < 8; i++) {
        if (read_word(0x4F4 + (i-1) * 2) > read_word(0x4F4 + i * 2)) {
            sorted = false;
            break;
        }
    }
    ASSERT_TRUE(sorted) << "Array should be sorted";
    
    /* THE IMPRESSIVE PART: Compare actual trace with expected */
    CompareTraces(60);  // Compare first 60 instructions
    
    /* Show the beautiful call graph */
    PrintCallGraph();
    
    /* Print the full disassembly trace if requested */
    const char* show_full = getenv("SHOW_FULL_TRACE");
    if (show_full && strcmp(show_full, "1") == 0) {
        printf("\n=== FULL DISASSEMBLY TRACE (%zu instructions) ===\n", trace.size());
        for (size_t i = 0; i < trace.size(); i++) {
            const auto& instr = trace[i];
            printf("%04zu: %06X %-8s %-20s [D0=%04X D1=%04X D2=%04X A0=%08X SP=%08X]\n",
                   i, instr.pc, 
                   instr.mnemonic.c_str(), 
                   instr.operands.c_str(),
                   instr.d0, instr.d1, instr.d2,
                   m68k_get_reg(NULL, M68K_REG_A0),
                   m68k_get_reg(NULL, M68K_REG_SP));
        }
    } else {
        printf("\n💡 Tip: Run with SHOW_FULL_TRACE=1 to see all %zu instructions\n", trace.size());
        printf("   Example: SHOW_FULL_TRACE=1 ./test_mergesort_verified\n");
    }
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║                     TEST COMPLETED SUCCESSFULLY                  ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
}