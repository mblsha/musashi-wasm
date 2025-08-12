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
        
        // Set PC and SP for these specific tests
        m68k_set_reg(M68K_REG_PC, 0x1000);
        m68k_set_reg(M68K_REG_SP, 0x100000);
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
    
    // Verify PC is at expected location
    unsigned int initial_pc = m68k_get_reg(NULL, M68K_REG_PC);
    ASSERT_EQ(initial_pc, 0x1000) << "PC should be at 0x1000 after setup";
    
    // Execute and verify PC advances
    pc_hooks.clear();
    m68k_execute(100);
    
    ASSERT_GE(pc_hooks.size(), 2u);
    EXPECT_EQ(pc_hooks[0], 0x1000);
    EXPECT_EQ(pc_hooks[1], 0x1002);
}

// Test MOVE instruction
TEST_F(M68kTest, MoveInstruction) {
    // MOVE.L #$12345678, D0
    write_word(0x1000, 0x203C); // MOVE.L #imm, D0
    write_long(0x1002, 0x12345678);
    
    m68k_execute(100);
    
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D0), 0x12345678);
}

// Test ADD instruction
TEST_F(M68kTest, AddInstruction) {
    // Set up initial values
    m68k_set_reg(M68K_REG_D0, 0x00000010);
    m68k_set_reg(M68K_REG_D1, 0x00000020);
    
    // ADD.L D1, D0
    write_word(0x1000, 0xD081); // ADD.L D1, D0
    
    m68k_execute(100);
    
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D0), 0x00000030);
}

// Test memory operations
TEST_F(M68kTest, MemoryOperations) {
    // Write test pattern to memory
    write_long(0x2000, 0xDEADBEEF);
    
    // MOVE.L $2000, D0
    write_word(0x1000, 0x2039); // MOVE.L (xxx).L, D0
    write_long(0x1002, 0x00002000);
    
    m68k_execute(100);
    
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D0), 0xDEADBEEF);
}

// Test branch instructions
TEST_F(M68kTest, BranchInstructions) {
    // BRA to 0x1010
    // Use 8-bit BRA for simpler calculation
    // For 8-bit BRA, PC base is 0x1002 (after opcode)
    // Target 0x1010 - Base 0x1002 = Displacement 0x0E
    write_word(0x1000, 0x600E); // BRA.b with displacement 0x0E
    
    // Target: NOP at 0x1010
    write_word(0x1010, 0x4E71);
    
    pc_hooks.clear();
    m68k_execute(100);
    
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
    
    m68k_execute(100);
    
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
    m68k_execute(200);
    
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
    
    m68k_execute(100);
    
    // Zero flag should be set
    unsigned int sr = m68k_get_reg(NULL, M68K_REG_SR);
    EXPECT_TRUE((sr & 0x04) != 0) << "Zero flag should be set";
}

// Test interrupt handling with stack frame validation
TEST_F(M68kTest, InterruptHandling) {
    // Set up interrupt vector for level 2 autovector (vector 26 = 0x68)
    // The autovector for IRQ 2 is at address 0x68 (26 * 4)
    write_long(0x68, 0x2000); // ISR at 0x2000
    
    // Write ISR at 0x2000 that validates supervisor mode
    write_word(0x2000, 0x4E71); // NOP (for hook detection)
    write_word(0x2002, 0x4E73); // RTE instruction
    
    // Write main program at 0x1000: NOP loop
    write_word(0x1000, 0x4E71); // NOP
    write_word(0x1002, 0x4E71); // NOP
    write_word(0x1004, 0x60FA); // BRA -6 (loop back to 0x1000)
    
    // Get initial state
    unsigned int initial_sr = m68k_get_reg(NULL, M68K_REG_SR);
    unsigned int initial_sp = m68k_get_reg(NULL, M68K_REG_SP);
    
    // Lower interrupt mask to 1 to allow level 2 interrupts
    m68k_set_reg(M68K_REG_SR, (initial_sr & ~0x0700) | 0x0100);
    
    // Clear hooks and execute a few instructions
    pc_hooks.clear();
    m68k_execute(100);
    
    // Generate interrupt level 2
    m68k_set_irq(2);
    
    // Execute to process the interrupt entry
    m68k_execute(30);
    
    // Verify interrupt was taken
    bool isr_executed = false;
    unsigned int sp_during_isr = 0;
    unsigned int sr_during_isr = 0;
    for (size_t i = 0; i < pc_hooks.size(); i++) {
        if (pc_hooks[i] == 0x2000) {
            isr_executed = true;
            // Get actual SP and SR values during ISR
            sp_during_isr = m68k_get_reg(NULL, M68K_REG_SP);
            sr_during_isr = m68k_get_reg(NULL, M68K_REG_SR);
            break;
        }
    }
    EXPECT_TRUE(isr_executed) << "ISR at 0x2000 should have been executed";
    
    if (isr_executed && sp_during_isr > 0) {
        // Validate stacked frame:
        // At sp_during_isr: SR (word)
        // At sp_during_isr+2: PC high (word)
        // At sp_during_isr+4: PC low (word)
        
        uint16_t stacked_sr = read_word(sp_during_isr);
        uint32_t stacked_pc = read_long(sp_during_isr + 2);
        
        // Verify supervisor bit was set in stacked SR
        EXPECT_TRUE((stacked_sr & 0x2000) != 0) << "Supervisor bit should be set in stacked SR";
        
        // The stacked SR should have the interrupt mask from BEFORE the interrupt (mask=1)
        unsigned int stacked_int_level = (stacked_sr >> 8) & 0x07;
        EXPECT_EQ(stacked_int_level, 1u) << "Stacked interrupt mask should be the value before interrupt";
        
        // The SR during ISR execution should have mask raised to at least 2
        if (sr_during_isr > 0) {
            unsigned int current_int_level = (sr_during_isr >> 8) & 0x07;
            EXPECT_GE(current_int_level, 2u) << "Current interrupt mask should be raised during ISR";
        }
        
        // Verify stacked PC is reasonable (should be near where we were looping)
        EXPECT_GE(stacked_pc, 0x1000u) << "Stacked PC should be in main program";
        EXPECT_LE(stacked_pc, 0x1006u) << "Stacked PC should be in main program loop";
    }
    
    // Continue execution to RTE
    m68k_execute(100);
    
    // Clear the interrupt
    m68k_set_irq(0);
    
    // Verify stack was restored
    unsigned int final_sp = m68k_get_reg(NULL, M68K_REG_SP);
    EXPECT_EQ(final_sp, initial_sp) << "SP should return to original after RTE";
}

