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
}

// Test fixture for M68k CPU tests
class M68kTest : public ::testing::Test {
protected:
    // Memory for the CPU
    std::vector<uint8_t> memory;
    static M68kTest* instance;
    
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
        return 0; // Continue execution
    }
    
    void SetUp() override {
        instance = this;
        memory.resize(1024 * 1024); // 1MB of memory
        std::fill(memory.begin(), memory.end(), 0);
        
        // Set up memory callbacks
        set_read_mem_func(read_mem);
        set_write_mem_func(write_mem);
        set_pc_hook_func(pc_hook);
        
        // Initialize M68k
        m68k_init();
        m68k_set_cpu_type(M68K_CPU_TYPE_68000);
        m68k_pulse_reset();
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

// Test CPU initialization
TEST_F(M68kTest, CPUInitialization) {
    // Check that registers are in expected state after reset
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_PC), 0);
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_SR), 0x2700); // Supervisor mode, interrupts disabled
    
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

// Test NOP instruction
TEST_F(M68kTest, NOPInstruction) {
    // NOP opcode is 0x4E71
    write_word(0x1000, 0x4E71);
    
    // Set PC to start of our code
    m68k_set_reg(M68K_REG_PC, 0x1000);
    
    // Execute one instruction
    m68k_execute(1);
    
    // PC should have advanced by 2 bytes
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_PC), 0x1002);
}

// Test MOVE immediate to data register
TEST_F(M68kTest, MOVEImmediate) {
    // MOVE.L #$12345678, D0 -> 0x203C 0x1234 0x5678
    write_word(0x1000, 0x203C);
    write_long(0x1002, 0x12345678);
    
    // Set PC to start of our code
    m68k_set_reg(M68K_REG_PC, 0x1000);
    m68k_set_reg(M68K_REG_D0, 0);
    
    // Execute one instruction
    m68k_execute(1);
    
    // Check that D0 contains the value
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D0), 0x12345678);
    
    // PC should have advanced by 6 bytes
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_PC), 0x1006);
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
    
    // Execute one instruction
    m68k_execute(1);
    
    // D0 should now be 30
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D0), 30);
    
    // D1 should be unchanged
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D1), 20);
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
    
    // Execute
    m68k_execute(1);
    
    // D0 should contain the value from memory
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D0), 0xCAFEBABE);
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
    m68k_execute(1);
    
    // Stack pointer should be decremented by 4
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_A7), 0x10000 - 4);
    
    // Value should be in memory
    unsigned int mem_value = (memory[0xFFFC] << 24) | (memory[0xFFFD] << 16) | 
                             (memory[0xFFFE] << 8) | memory[0xFFFF];
    EXPECT_EQ(mem_value, 0x12345678);
    
    // Now pop it back: MOVE.L (A7)+, D1 -> 0x221F
    write_word(0x1002, 0x221F);
    
    m68k_set_reg(M68K_REG_D1, 0);
    m68k_execute(1);
    
    // D1 should have the value
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D1), 0x12345678);
    
    // Stack pointer should be back to original
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_A7), 0x10000);
}

// Test conditional flags
TEST_F(M68kTest, ConditionalFlags) {
    // CMP.L D0, D1 (compare D0 to D1)
    m68k_set_reg(M68K_REG_D0, 10);
    m68k_set_reg(M68K_REG_D1, 10);
    
    // CMP.L D0, D1 -> 0xB280
    write_word(0x1000, 0xB280);
    
    m68k_set_reg(M68K_REG_PC, 0x1000);
    m68k_execute(1);
    
    // Check status register - Zero flag should be set
    unsigned int sr = m68k_get_reg(NULL, M68K_REG_SR);
    EXPECT_TRUE(sr & 0x04); // Z flag
    
    // Compare different values
    m68k_set_reg(M68K_REG_D0, 5);
    m68k_set_reg(M68K_REG_D1, 10);
    
    write_word(0x1002, 0xB280);
    m68k_execute(1);
    
    sr = m68k_get_reg(NULL, M68K_REG_SR);
    EXPECT_FALSE(sr & 0x04); // Z flag should be clear
}

// Test CPU cycles
TEST_F(M68kTest, CPUCycles) {
    // NOP instruction
    write_word(0x1000, 0x4E71);
    write_word(0x1002, 0x4E71);
    write_word(0x1004, 0x4E71);
    
    m68k_set_reg(M68K_REG_PC, 0x1000);
    
    // Execute with cycle count
    int cycles = m68k_execute(10); // Execute up to 10 cycles
    
    // Should have executed some cycles
    EXPECT_GT(cycles, 0);
    
    // PC should have advanced
    EXPECT_GT(m68k_get_reg(NULL, M68K_REG_PC), 0x1000);
}