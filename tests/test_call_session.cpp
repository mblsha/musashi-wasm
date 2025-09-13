// C++ tests for sentinel return session in myfunc.cc

#include "m68k_test_common.h"

extern "C" {
    unsigned long long m68k_call_until_js_stop(unsigned int entry_pc, unsigned int timeslice);
    unsigned int m68k_get_address_space_max();
}

class CallSessionTest : public M68kMinimalTestBase<CallSessionTest> {
public:
    // Stop when PC reaches this value
    unsigned int stop_pc = 0;

    int OnPcHook(unsigned int pc) override {
        pc_hooks.push_back(pc);
        if (stop_pc != 0 && pc == stop_pc) {
            return 1; // request stop
        }
        return 0;
    }
};

TEST_F(CallSessionTest, SimpleCallStopsOnOverridePc) {
    // Sub at 0x0410: MOVE.L #$CAFEBABE,D2 ; RTS
    write_word(0x0410, 0x243C); // MOVE.L #imm, D2
    write_long(0x0412, 0xCAFEBABE);
    write_word(0x0416, 0x4E75); // RTS

    stop_pc = 0x0416; // stop at RTS

    auto cycles = m68k_call_until_js_stop(0x0410, 1'000'000);
    EXPECT_GT(cycles, 0ull);

    // D2 updated
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D2), 0xCAFEBABEu);

    // PC parked at sentinel (max address, even)
    unsigned int max = m68k_get_address_space_max() & 0x00FFFFFEu;
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_PC), max);
}

TEST_F(CallSessionTest, NestedCallsStopOnlyAtOuterRts) {
    // Sub B at 0x0520: MOVE.L #$DEADBEEF,D2 ; RTS
    write_word(0x0520, 0x243C);
    write_long(0x0522, 0xDEADBEEFu);
    write_word(0x0526, 0x4E75);

    // Sub A at 0x0500: JSR $00000520 ; ADD.L #1, D2 ; RTS
    write_word(0x0500, 0x4EB9); // JSR absolute long
    write_long(0x0502, 0x00000520u);
    write_word(0x0506, 0x0682); // ADD.L #1, D2
    write_long(0x0508, 0x00000001u);
    write_word(0x050C, 0x4E75); // RTS (outer)

    stop_pc = 0x050C; // only stop at outer RTS

    auto cycles = m68k_call_until_js_stop(0x0500, 2'000'000);
    EXPECT_GT(cycles, 0ull);
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D2), (0xDEADBEEFu + 1) & 0xFFFFFFFFu);

    unsigned int max = m68k_get_address_space_max() & 0x00FFFFFEu;
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_PC), max);
}

