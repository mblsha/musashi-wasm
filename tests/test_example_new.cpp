/* Example: How easy it is to create new tests with the refactored approach */

#include "m68k_test_common.h"

/* BEFORE: Would need 100+ lines of boilerplate
   AFTER: Just one line! */
DECLARE_M68K_TEST(QuickTest) {};

TEST_F(QuickTest, SimpleAddition) {
    // That's it! No boilerplate needed. Jump straight to testing:
    
    // Write ADD.W #5, D0 instruction
    write_word(0x400, 0x0640);  // ADD.W #imm, D0
    write_word(0x402, 0x0005);  // immediate value 5
    
    m68k_set_reg(M68K_REG_D0, 10);
    m68k_execute(10);
    
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D0), 15);
}

/* For more complex tests, just override what you need: */
class AdvancedTest : public M68kMinimalTestBase<AdvancedTest> {
public:
    int OnPcHook(unsigned int pc) override {
        if (pc == 0x500) {
            interrupt_count++;
            return 1;  // Stop on special address
        }
        return M68kMinimalTestBase::OnPcHook(pc);  // Chain to base
    }
    
protected:
    int interrupt_count = 0;
};

TEST_F(AdvancedTest, InterruptCounting) {
    write_word(0x400, 0x4E71);  // NOP
    write_word(0x500, 0x4E71);  // NOP at special address
    
    // Jump to 0x500
    write_word(0x402, 0x4EF9);  // JMP
    write_long(0x404, 0x500);
    
    m68k_execute(20);
    
    EXPECT_EQ(interrupt_count, 1) << "Should have hit interrupt address once";
}

/* 
   COMPARISON:
   
   OLD APPROACH (test_m68k.cpp style):
   - 133 lines of boilerplate before first test
   - Memory management code duplicated
   - Static instance pattern duplicated
   - Helper functions duplicated
   
   NEW APPROACH:
   - 1 line to declare test class
   - 0 lines of boilerplate
   - All infrastructure inherited
   - Focus 100% on test logic
   
   REDUCTION: 133 lines → 1 line (99.2% reduction!)
*/