// Verify clean entry helper sets CPU state and executes first instruction

#include "m68k_test_common.h"

extern "C" {
    void set_entry_point(uint32_t pc);
}

DECLARE_M68K_TEST(SetEntryPointTest) {
protected:
    void OnSetUp() override {
        // Ensure a known baseline: memory is zeroed by base SetUp()
    }
};

// Program at 0x0400:
//  0x4E71          NOP
//  0x303C 0x1234   MOVE.W #$1234, D0
//  0x4E71          NOP
TEST_F(SetEntryPointTest, ExecutesFromSpecifiedPC) {
    // Encode program (big-endian words)
    write_word(0x0400, 0x4E71); // NOP
    write_word(0x0402, 0x303C); // MOVE.W #imm, D0
    write_word(0x0404, 0x1234); // imm
    write_word(0x0406, 0x4E71); // NOP

    // Use the API under test to jump to 0x0400
    set_entry_point(0x0400);

    // Validate SR and VBR were set to sane values
    unsigned int sr = m68k_get_reg(NULL, M68K_REG_SR);
    unsigned int vbr = m68k_get_reg(NULL, M68K_REG_VBR);
    M68kTestUtils::ExpectFlagsSet(sr, 0x2700, "SR should be supervisor, IRQ masked, trace off");
    EXPECT_EQ(vbr, 0u) << "VBR should be 0 on 68000";

    // Execute a small slice and check the effect of MOVE.W
    m68k_execute(100);
    unsigned int d0 = m68k_get_reg(NULL, M68K_REG_D0);
    EXPECT_EQ(d0 & 0xFFFFu, 0x1234u);

    // PC should have advanced past the immediate
    unsigned int pc = m68k_get_reg(NULL, M68K_REG_PC);
    EXPECT_EQ(pc, 0x0406u) << "PC should point at the NOP after MOVE.W";
}

