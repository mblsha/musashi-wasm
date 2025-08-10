/* Refactored M68K tests using minimal base class - eliminates ~100 lines of duplication */

#include "m68k_test_common.h"

extern "C" {
    void add_region(unsigned int start, unsigned int size, void* data);
    void enable_printf_logging();
}

/* Define test class using the macro */
DECLARE_M68K_TEST(M68kTest) {
public:
    // Override PC hook for execution control
    int OnPcHook(unsigned int pc) override {
        pc_hooks.push_back(pc);
        
        // Check if we should stop execution after this instruction
        if (stop_on_next_hook && pc_hooks.size() >= 2) {
            return 1; // Stop execution  
        }
        
        if (stop_after_pc != 0 && pc > stop_after_pc) {
            return 1; // Stop execution
        }
        
        return 0; // Continue
    }
    
protected:
    // Control execution - specific to these tests
    unsigned int stop_after_pc = 0;
    bool stop_on_next_hook = false;
    
    void OnSetUp() override {
        // Reset execution control
        stop_after_pc = 0;
        stop_on_next_hook = false;
        
        // Override reset vector for these specific tests
        write_long(0, 0x100000);  // SP = 0x100000
        write_long(4, 0x1000);    // PC = 0x1000
    }
};

// Test CPU initialization
TEST_F(M68kTest, CPUInitialization) {
    // Check that registers are in expected state after reset
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_PC), 0x1000);
    
    // Status register should have supervisor bit and interrupt mask set
    unsigned int sr = m68k_get_reg(NULL, M68K_REG_SR);
    M68kTestUtils::ExpectFlagsSet(sr, 0x2700, "Supervisor mode and interrupt bits");
    
    // Data registers should be undefined but readable
    for (int i = M68K_REG_D0; i <= M68K_REG_D7; i++) {
        m68k_get_reg(NULL, static_cast<m68k_register_t>(i)); // Just ensure no crash
    }
}

// Test basic instruction execution
TEST_F(M68kTest, BasicInstructionExecution) {
    // Write NOP instruction at PC (0x1000)
    write_word(0x1000, 0x4E71); // NOP
    write_word(0x1002, 0x4E71); // NOP
    
    // Execute and verify PC advances
    pc_hooks.clear();
    m68k_execute(10);
    
    ASSERT_GE(pc_hooks.size(), 2u);
    EXPECT_EQ(pc_hooks[0], 0x1000);
    EXPECT_EQ(pc_hooks[1], 0x1002);
}

// Test MOVE instruction
TEST_F(M68kTest, MoveInstruction) {
    // MOVE.L #$12345678, D0
    write_word(0x1000, 0x203C); // MOVE.L #imm, D0
    write_long(0x1002, 0x12345678);
    
    m68k_execute(10);
    
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D0), 0x12345678);
}

// Test ADD instruction
TEST_F(M68kTest, AddInstruction) {
    // Set up initial values
    m68k_set_reg(M68K_REG_D0, 0x00000010);
    m68k_set_reg(M68K_REG_D1, 0x00000020);
    
    // ADD.L D1, D0
    write_word(0x1000, 0xD081); // ADD.L D1, D0
    
    m68k_execute(10);
    
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D0), 0x00000030);
}

// Test memory operations
TEST_F(M68kTest, MemoryOperations) {
    // Write test pattern to memory
    write_long(0x2000, 0xDEADBEEF);
    
    // MOVE.L $2000, D0
    write_word(0x1000, 0x2039); // MOVE.L (xxx).L, D0
    write_long(0x1002, 0x00002000);
    
    m68k_execute(10);
    
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D0), 0xDEADBEEF);
}

// Test branch instructions
TEST_F(M68kTest, BranchInstructions) {
    // BRA to 0x1010
    write_word(0x1000, 0x6000); // BRA
    write_word(0x1002, 0x000E); // Offset to 0x1010
    
    // Target: NOP at 0x1010
    write_word(0x1010, 0x4E71);
    
    pc_hooks.clear();
    m68k_execute(20);
    
    // Should see PC at 0x1000 then 0x1010
    ASSERT_GE(pc_hooks.size(), 2u);
    EXPECT_EQ(pc_hooks[0], 0x1000);
    EXPECT_EQ(pc_hooks[1], 0x1010);
}

// Test stack operations
TEST_F(M68kTest, StackOperations) {
    // Initial SP is at 0x100000
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_SP), 0x100000);
    
    // MOVE.L #$12345678, -(SP)
    write_word(0x1000, 0x2F3C); // MOVE.L #imm, -(SP)
    write_long(0x1002, 0x12345678);
    
    m68k_execute(10);
    
    // SP should be decremented by 4
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_SP), 0xFFFFC);
    
    // Value should be at new SP location
    EXPECT_EQ(read_long(0xFFFFC), 0x12345678);
}

// Test subroutine calls
TEST_F(M68kTest, SubroutineCalls) {
    // JSR to 0x2000
    write_word(0x1000, 0x4EB9); // JSR (xxx).L
    write_long(0x1002, 0x00002000);
    
    // Subroutine at 0x2000: RTS
    write_word(0x2000, 0x4E75); // RTS
    
    pc_hooks.clear();
    m68k_execute(50);
    
    // Should see: 0x1000 (JSR), 0x2000 (subroutine), 0x1006 (return)
    ASSERT_GE(pc_hooks.size(), 3u);
    EXPECT_EQ(pc_hooks[0], 0x1000); // JSR
    EXPECT_EQ(pc_hooks[1], 0x2000); // Subroutine
    EXPECT_EQ(pc_hooks[2], 0x1006); // After return
}

// Test condition codes
TEST_F(M68kTest, ConditionCodes) {
    // CMP.W #0, D0 (with D0 = 0)
    m68k_set_reg(M68K_REG_D0, 0);
    write_word(0x1000, 0x0C40); // CMP.W #imm, D0
    write_word(0x1002, 0x0000);
    
    m68k_execute(10);
    
    // Zero flag should be set
    unsigned int sr = m68k_get_reg(NULL, M68K_REG_SR);
    EXPECT_TRUE((sr & 0x04) != 0) << "Zero flag should be set";
}

// Test interrupt handling (basic)
TEST_F(M68kTest, InterruptBasics) {
    // Current interrupt mask is 7 (all masked)
    unsigned int sr = m68k_get_reg(NULL, M68K_REG_SR);
    EXPECT_EQ((sr >> 8) & 7, 7) << "Interrupt mask should be 7";
    
    // Lower interrupt mask
    m68k_set_reg(M68K_REG_SR, sr & ~0x0700); // Clear interrupt mask
    
    // Generate interrupt level 2
    m68k_set_irq(2);
    
    // Should process interrupt (implementation specific)
    m68k_execute(10);
    
    // Just verify no crash - actual behavior depends on vector setup
    SUCCEED();
}

// Test execution control via PC hook
TEST_F(M68kTest, ExecutionControl) {
    // Write several NOPs
    for (int i = 0; i < 10; i++) {
        write_word(0x1000 + i * 2, 0x4E71);
    }
    
    // Stop after PC > 0x1004
    stop_after_pc = 0x1004;
    
    pc_hooks.clear();
    m68k_execute(100);
    
    // Should have stopped early
    ASSERT_LE(pc_hooks.size(), 4u);
    EXPECT_LE(pc_hooks.back(), 0x1006u);
}

// Test single-step execution
TEST_F(M68kTest, SingleStep) {
    // Write NOPs
    write_word(0x1000, 0x4E71);
    write_word(0x1002, 0x4E71);
    
    pc_hooks.clear();
    
    // Execute one instruction at a time
    for (int i = 0; i < 3; i++) {
        m68k_execute(1);
    }
    
    ASSERT_EQ(pc_hooks.size(), 3u);
    EXPECT_EQ(pc_hooks[0], 0x1000);
    EXPECT_EQ(pc_hooks[1], 0x1002);
    EXPECT_EQ(pc_hooks[2], 0x1004);
}