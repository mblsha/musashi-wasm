#include <gtest/gtest.h>
#include <cstring>
#include <vector>

extern "C" {
#include "m68k.h"

// Functions from myfunc.c
void set_read_mem_func(int (*func)(unsigned int address, int size));
void set_write_mem_func(void (*func)(unsigned int address, int size, unsigned int value));
void set_pc_hook_func(int (*func)(unsigned int pc));
void add_region(unsigned int start, unsigned int size, void* data);
void enable_printf_logging();
}

// Test fixture for M68k CPU tests
class M68kTest : public ::testing::Test {
protected:
    // Memory for the CPU
    std::vector<uint8_t> memory;
    std::vector<unsigned int> pc_hooks;
    static M68kTest* instance;
    
    // Control execution
    static unsigned int stop_after_pc;  // Stop execution after this PC
    static bool stop_on_next_hook;
    
    // Memory callbacks
    static int read_mem(unsigned int address, int size) {
        if (!instance || address >= instance->memory.size()) {
            return 0;
        }
        
        if (size == 1) {
            return instance->memory[address];
        } else if (size == 2 && address + 1 < instance->memory.size()) {
            return (instance->memory[address] << 8) | 
                   instance->memory[address + 1];
        } else if (size == 4 && address + 3 < instance->memory.size()) {
            return (instance->memory[address] << 24) |
                   (instance->memory[address + 1] << 16) |
                   (instance->memory[address + 2] << 8) |
                   instance->memory[address + 3];
        }
        
        return 0;
    }
    
    static void write_mem(unsigned int address, int size, unsigned int value) {
        if (!instance || address >= instance->memory.size()) {
            return;
        }
        
        if (size == 1) {
            instance->memory[address] = value & 0xFF;
        } else if (size == 2 && address + 1 < instance->memory.size()) {
            instance->memory[address] = (value >> 8) & 0xFF;
            instance->memory[address + 1] = value & 0xFF;
        } else if (size == 4 && address + 3 < instance->memory.size()) {
            instance->memory[address] = (value >> 24) & 0xFF;
            instance->memory[address + 1] = (value >> 16) & 0xFF;
            instance->memory[address + 2] = (value >> 8) & 0xFF;
            instance->memory[address + 3] = value & 0xFF;
        }
    }
    
    static int pc_hook(unsigned int pc) {
        if (instance) {
            instance->pc_hooks.push_back(pc);
            
            // Check if we should stop execution after this instruction
            if (stop_on_next_hook && instance->pc_hooks.size() >= 2) {
                // Stop after executing one more instruction beyond the first
                return 1; // Stop execution  
            }
            
            if (stop_after_pc != 0 && pc > stop_after_pc) {
                // Stop when we pass the target PC
                return 1; // Stop execution
            }
        }
        return 0; // Continue execution
    }
    
    void SetUp() override {
        instance = this;
        memory.resize(1024 * 1024); // 1MB of memory
        std::fill(memory.begin(), memory.end(), 0);
        
        // Reset execution control
        stop_after_pc = 0;
        stop_on_next_hook = false;
        pc_hooks.clear();
        
        // Set up memory callbacks
        set_read_mem_func(read_mem);
        set_write_mem_func(write_mem);
        set_pc_hook_func(pc_hook);
        
        // Set up reset vector
        // Initial Stack Pointer at address 0 (set to 0x100000)
        memory[0] = 0x00; memory[1] = 0x10; memory[2] = 0x00; memory[3] = 0x00;
        // Initial Program Counter at address 4 (set to 0x1000)
        memory[4] = 0x00; memory[5] = 0x00; memory[6] = 0x10; memory[7] = 0x00;
        
        // Initialize M68k
        m68k_init();
        m68k_set_cpu_type(M68K_CPU_TYPE_68000);
        m68k_pulse_reset();
        
        // After reset, the first execution needs some initialization cycles
        // Run a dummy execution to get past this
        m68k_execute(1);
    }
    
    void TearDown() override {
        instance = nullptr;
    }
    
    // Helper to write a 16-bit word to memory
    void write_word(unsigned int address, unsigned int value) {
        memory[address] = (value >> 8) & 0xFF;
        memory[address + 1] = value & 0xFF;
    }
    
    // Helper to write a 32-bit long to memory  
    void write_long(unsigned int address, unsigned int value) {
        memory[address] = (value >> 24) & 0xFF;
        memory[address + 1] = (value >> 16) & 0xFF;
        memory[address + 2] = (value >> 8) & 0xFF;
        memory[address + 3] = value & 0xFF;
    }
};

M68kTest* M68kTest::instance = nullptr;
unsigned int M68kTest::stop_after_pc = 0;
bool M68kTest::stop_on_next_hook = false;

// Test CPU initialization
TEST_F(M68kTest, CPUInitialization) {
    // Check that registers are in expected state after reset
    // PC should be set to the value from the reset vector at address 4
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_PC), 0x1000);
    // Status register should have supervisor bit and interrupt mask set
    // The exact value after reset may include Zero flag (bit 2)
    unsigned int sr = m68k_get_reg(NULL, M68K_REG_SR);
    EXPECT_TRUE((sr & 0x2700) == 0x2700); // Check supervisor mode and interrupt bits
    
    // Data registers should be undefined but readable
    for (int i = M68K_REG_D0; i <= M68K_REG_D7; i++) {
        m68k_get_reg(NULL, (m68k_register_t)i); // Should not crash
    }
    
    // Address registers should be undefined but readable
    for (int i = M68K_REG_A0; i <= M68K_REG_A7; i++) {
        m68k_get_reg(NULL, (m68k_register_t)i); // Should not crash
    }
}

// Test register get/set
TEST_F(M68kTest, RegisterAccess) {
    // Set and get data registers
    m68k_set_reg(M68K_REG_D0, 0x12345678);
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D0), 0x12345678);
    
    m68k_set_reg(M68K_REG_D7, 0xDEADBEEF);
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D7), 0xDEADBEEF);
    
    // Set and get address registers
    m68k_set_reg(M68K_REG_A0, 0xAABBCCDD);
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_A0), 0xAABBCCDD);
    
    m68k_set_reg(M68K_REG_A7, 0x00100000);
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_A7), 0x00100000);
    
    // Set and get PC
    m68k_set_reg(M68K_REG_PC, 0x1000);
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_PC), 0x1000);
}

// Debug test - DISABLED
TEST_F(M68kTest, DISABLED_ADDDebug) {
    // Setup: D0 = 10, D1 = 20
    m68k_set_reg(M68K_REG_D0, 10);
    m68k_set_reg(M68K_REG_D1, 20);
    
    // ADD.L D1, D0 -> 0xD081
    write_word(0x1000, 0xD081);
    // Put NOPs after
    write_word(0x1002, 0x4E71);
    write_word(0x1004, 0x4E71);
    
    printf("\n=== ADD Instruction Debug ===\n");
    printf("Before: D0=%d, D1=%d\n", m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1));
    
    // Set PC to start of our code
    m68k_set_reg(M68K_REG_PC, 0x1000);
    pc_hooks.clear();
    
    // Try to execute with enough cycles
    int cycles = m68k_execute(30);
    
    printf("After: D0=%d, D1=%d\n", m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1));
    printf("PC advanced from 0x1000 to 0x%06X\n", m68k_get_reg(NULL, M68K_REG_PC));
    printf("Cycles used: %d\n", cycles);
    printf("Hooks called: %zu\n", pc_hooks.size());
    for (size_t i = 0; i < pc_hooks.size(); i++) {
        printf("  Hook %zu: PC=0x%06X\n", i, pc_hooks[i]);
    }
    
    SUCCEED();
}

// Test NOP instruction
TEST_F(M68kTest, NOPInstruction) {
    // NOP opcode is 0x4E71
    write_word(0x1000, 0x4E71);
    
    // Set PC to start of our code
    m68k_set_reg(M68K_REG_PC, 0x1000);
    
    // Clear hooks to track this specific execution
    pc_hooks.clear();
    
    // Execute one NOP instruction
    int cycles = m68k_execute(4);
    
    // PC hook should have been called for the NOP instruction
    ASSERT_GE(pc_hooks.size(), 1) << "Expected at least 1 PC hook but got " << pc_hooks.size();
    EXPECT_EQ(pc_hooks[0], 0x1000) << "First hook should be at NOP instruction";
    
    // PC should have advanced by 2 bytes (NOP is 2 bytes)
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_PC), 0x1002);
    
    // Cycles should be positive (instruction was executed)
    EXPECT_GT(cycles, 0);
}

// Test MOVE immediate to data register
TEST_F(M68kTest, MOVEImmediate) {
    // MOVE.L #$12345678, D0 -> 0x203C 0x1234 0x5678
    write_word(0x1000, 0x203C);
    write_long(0x1002, 0x12345678);
    
    // Set PC to start of our code
    m68k_set_reg(M68K_REG_PC, 0x1000);
    m68k_set_reg(M68K_REG_D0, 0);
    
    // Clear hooks
    pc_hooks.clear();
    
    // Execute MOVE.L immediate
    int cycles = m68k_execute(12);
    
    // PC hook should have been called for the MOVE instruction
    ASSERT_GE(pc_hooks.size(), 1) << "Expected at least one PC hook call";
    EXPECT_EQ(pc_hooks[0], 0x1000) << "First hook should be at MOVE instruction";
    
    // Check that D0 contains the value
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D0), 0x12345678);
    
    // PC should have advanced by 6 bytes (2 for opcode + 4 for immediate)
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_PC), 0x1006);
    
    // Cycles should be positive
    EXPECT_GT(cycles, 0);
}

// Test ADD instruction
TEST_F(M68kTest, ADDInstruction) {
    // Setup: D0 = 10, D1 = 20
    m68k_set_reg(M68K_REG_D0, 10);
    m68k_set_reg(M68K_REG_D1, 20);
    
    // ADD.L D1, D0 -> 0xD081
    write_word(0x1000, 0xD081);
    
    // Set PC to start of our code
    m68k_set_reg(M68K_REG_PC, 0x1000);
    
    // Clear hooks
    pc_hooks.clear();
    
    // Execute with limited cycles to try to execute just one instruction
    int cycles = m68k_execute(8);
    
    // D0 should now be 30
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D0), 30);
    
    // D1 should be unchanged
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D1), 20);
    
    // PC hook should have been called at least once for the ADD instruction
    ASSERT_GE(pc_hooks.size(), 1) << "Expected at least one PC hook call";
    EXPECT_EQ(pc_hooks[0], 0x1000) << "First hook should be at ADD instruction";
    
    // PC should have advanced past the ADD instruction
    EXPECT_GT(m68k_get_reg(NULL, M68K_REG_PC), 0x1000);
    
    // Cycles should be positive (instruction was executed)
    EXPECT_GT(cycles, 0);
}

// Test memory operations  
TEST_F(M68kTest, MemoryOperations) {
    // Write test value to memory
    write_long(0x2000, 0xCAFEBABE);
    
    // MOVE.L $2000, D0 -> 0x2038 0x2000
    write_word(0x1000, 0x2038);
    write_word(0x1002, 0x2000);
    
    // Set PC and clear D0
    m68k_set_reg(M68K_REG_PC, 0x1000);
    m68k_set_reg(M68K_REG_D0, 0);
    
    // Clear hooks
    pc_hooks.clear();
    
    // Execute MOVE.L from absolute address
    int cycles = m68k_execute(16);
    
    // PC hook should have been called for the MOVE instruction
    ASSERT_GE(pc_hooks.size(), 1) << "Expected at least one PC hook call";
    EXPECT_EQ(pc_hooks[0], 0x1000) << "First hook should be at MOVE instruction";
    
    // D0 should contain the value from memory
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D0), 0xCAFEBABE);
    
    // PC should have advanced by 4 bytes
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_PC), 0x1004);
    
    // Cycles should be positive
    EXPECT_GT(cycles, 0);
}

// Test stack operations
TEST_F(M68kTest, StackOperations) {
    // Set up stack pointer
    m68k_set_reg(M68K_REG_A7, 0x10000);
    
    // Set a value in D0
    m68k_set_reg(M68K_REG_D0, 0x12345678);
    
    // MOVE.L D0, -(A7) -> 0x2F00 (push D0)
    write_word(0x1000, 0x2F00);
    
    m68k_set_reg(M68K_REG_PC, 0x1000);
    pc_hooks.clear();
    
    // Execute push instruction
    int cycles = m68k_execute(12);
    
    // PC hook should have been called for the push instruction
    ASSERT_GE(pc_hooks.size(), 1) << "Expected at least one PC hook call for push";
    EXPECT_EQ(pc_hooks[0], 0x1000) << "First hook should be at push instruction";
    
    // Stack pointer should be decremented by 4
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_A7), 0x10000 - 4);
    
    // Value should be in memory
    unsigned int mem_value = (memory[0xFFFC] << 24) | (memory[0xFFFD] << 16) | 
                             (memory[0xFFFE] << 8) | memory[0xFFFF];
    EXPECT_EQ(mem_value, 0x12345678);
    
    // Now pop it back: MOVE.L (A7)+, D1 -> 0x221F
    write_word(0x1002, 0x221F);
    
    m68k_set_reg(M68K_REG_D1, 0);
    m68k_set_reg(M68K_REG_PC, 0x1002);
    pc_hooks.clear();
    
    // Execute pop instruction
    cycles = m68k_execute(12);
    
    // PC hook should have been called for the pop instruction
    ASSERT_GE(pc_hooks.size(), 1) << "Expected at least one PC hook call for pop";
    EXPECT_EQ(pc_hooks[0], 0x1002) << "First hook should be at pop instruction";
    
    // D1 should have the value
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D1), 0x12345678);
    
    // Stack pointer should be back to original
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_A7), 0x10000);
    
    // Cycles should be positive
    EXPECT_GT(cycles, 0);
}

// Test conditional flags
TEST_F(M68kTest, ConditionalFlags) {
    // Simple SUB to test flags (SUB.L D1, D0)
    m68k_set_reg(M68K_REG_D0, 10);
    m68k_set_reg(M68K_REG_D1, 10);
    
    // SUB.L D1, D0 -> 0x9081 (D0 = D0 - D1)
    write_word(0x1000, 0x9081);
    
    m68k_set_reg(M68K_REG_PC, 0x1000);
    pc_hooks.clear();
    
    // Execute SUB instruction
    int cycles = m68k_execute(10);
    
    // PC hook should have been called for the SUB instruction
    ASSERT_GE(pc_hooks.size(), 1) << "Expected at least one PC hook call for SUB";
    EXPECT_EQ(pc_hooks[0], 0x1000) << "First hook should be at SUB instruction";
    
    // D0 should be 0 (10 - 10)
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D0), 0);
    
    // Check status register - Zero flag should be set since result is 0
    unsigned int sr = m68k_get_reg(NULL, M68K_REG_SR);
    EXPECT_TRUE(sr & 0x04) << "Zero flag should be set when result is zero";
    
    // Subtract different values (5 - 10 should be negative)
    m68k_set_reg(M68K_REG_D0, 5);
    m68k_set_reg(M68K_REG_D1, 10);
    
    write_word(0x1002, 0x9081);
    m68k_set_reg(M68K_REG_PC, 0x1002);
    pc_hooks.clear();
    
    // Execute second SUB
    cycles = m68k_execute(10);
    
    // PC hook should have been called for the second SUB instruction
    ASSERT_GE(pc_hooks.size(), 1) << "Expected at least one PC hook call for second SUB";
    EXPECT_EQ(pc_hooks[0], 0x1002) << "First hook should be at second SUB instruction";
    
    // D0 should be negative (5 - 10 = -5, or 0xFFFFFFFB in unsigned)
    unsigned int result = m68k_get_reg(NULL, M68K_REG_D0);
    EXPECT_EQ(result, 0xFFFFFFFB) << "D0 should be -5 (0xFFFFFFFB)";
    
    sr = m68k_get_reg(NULL, M68K_REG_SR);
    EXPECT_FALSE(sr & 0x04) << "Zero flag should be clear when result is non-zero";
    
    // Cycles should be positive (instructions were executed)
    EXPECT_GT(cycles, 0);
}

// Test CPU cycles and PC hooks
TEST_F(M68kTest, CPUCycles) {
    // NOP instructions (each takes 4 cycles)
    write_word(0x1000, 0x4E71);
    write_word(0x1002, 0x4E71);
    write_word(0x1004, 0x4E71);
    write_word(0x1006, 0x4E71);
    write_word(0x1008, 0x4E71);
    
    m68k_set_reg(M68K_REG_PC, 0x1000);
    pc_hooks.clear();
    
    // Execute with limited cycles (should execute 5 NOPs = 20 cycles)
    int cycles = m68k_execute(20);
    
    // Should have executed 5 NOPs
    EXPECT_EQ(pc_hooks.size(), 5) << "Should have 5 PC hook calls (one per NOP)";
    
    // Verify each hook was at the expected address
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(pc_hooks[i], 0x1000 + i * 2) << "Hook " << i << " at wrong address";
    }
    
    // PC should have advanced by 10 bytes (5 NOPs * 2 bytes each)
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_PC), 0x100A);
    
    // Cycles should be positive
    EXPECT_GT(cycles, 0);
    
    // Verify we can track a single instruction
    m68k_set_reg(M68K_REG_PC, 0x1000);
    pc_hooks.clear();
    
    cycles = m68k_execute(4);
    
    // Should have one hook call
    EXPECT_EQ(pc_hooks.size(), 1) << "Should have 1 PC hook call for single NOP";
    EXPECT_EQ(pc_hooks[0], 0x1000) << "Hook should be at NOP instruction";
    
    // PC should have advanced by 2 bytes
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_PC), 0x1002);
    
    // Cycles should be positive
    EXPECT_GT(cycles, 0);
}