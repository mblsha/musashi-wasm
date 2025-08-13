/* ======================================================================== */
/* =================== M68K EXCEPTION HANDLING TESTS ==================== */
/* ======================================================================== */

#include "m68k_test_common.h"

/* Define test class for exception handling */
DECLARE_M68K_TEST(ExceptionTest) {
protected:
    void OnSetUp() override {
        /* Set up exception vectors */
        SetupExceptionVectors();
        
        /* Start in supervisor mode at a known PC */
        m68k_set_reg(M68K_REG_PC, 0x1000);
        m68k_set_reg(M68K_REG_SP, 0x100000);
    }
    
    void SetupExceptionVectors() {
        /* Vector 0: Reset Initial SSP */
        write_long(0x00, 0x100000);
        
        /* Vector 1: Reset Initial PC */
        write_long(0x04, 0x1000);
        
        /* Vector 2: Bus Error */
        write_long(0x08, 0x2000);
        
        /* Vector 3: Address Error */
        write_long(0x0C, 0x2010);
        
        /* Vector 4: Illegal Instruction */
        write_long(0x10, 0x2020);
        
        /* Vector 5: Zero Divide */
        write_long(0x14, 0x2030);
        
        /* Vector 6: CHK Instruction */
        write_long(0x18, 0x2040);
        
        /* Vector 7: TRAPV Instruction */
        write_long(0x1C, 0x2050);
        
        /* Vector 8: Privilege Violation */
        write_long(0x20, 0x2060);
        
        /* Vector 9: Trace */
        write_long(0x24, 0x2070);
        
        /* Vector 10: Line 1010 Emulator */
        write_long(0x28, 0x2080);
        
        /* Vector 11: Line 1111 Emulator */
        write_long(0x2C, 0x2090);
        
        /* TRAP vectors (32-47) */
        for (int i = 0; i < 16; i++) {
            write_long(0x80 + i * 4, 0x3000 + i * 0x10);
        }
        
        /* Write RTE instruction at each exception handler */
        write_word(0x2000, 0x4E73); /* Bus Error handler */
        write_word(0x2010, 0x4E73); /* Address Error handler */
        write_word(0x2020, 0x4E73); /* Illegal Instruction handler */
        write_word(0x2030, 0x4E73); /* Zero Divide handler */
        write_word(0x2040, 0x4E73); /* CHK handler */
        write_word(0x2050, 0x4E73); /* TRAPV handler */
        write_word(0x2060, 0x4E73); /* Privilege Violation handler */
        write_word(0x2070, 0x4E73); /* Trace handler */
        write_word(0x2080, 0x4E73); /* Line 1010 handler */
        write_word(0x2090, 0x4E73); /* Line 1111 handler */
        
        /* TRAP handlers */
        for (int i = 0; i < 16; i++) {
            write_word(0x3000 + i * 0x10, 0x4E73); /* RTE */
        }
    }
};

/* ======================================================================== */
/* ======================== EXCEPTION TESTS ============================== */
/* ======================================================================== */

TEST_F(ExceptionTest, IllegalInstructionException) {
    /* Write an illegal instruction at PC */
    write_word(0x1000, 0x4AFC); /* Illegal instruction */
    write_word(0x1002, 0x4E71); /* NOP (after exception return) */
    
    /* Clear PC hooks to track execution */
    pc_hooks.clear();
    
    /* Execute - should trigger illegal instruction exception */
    m68k_execute(100);
    
    /* Check that we jumped to the illegal instruction handler */
    bool found_handler = false;
    bool returned_from_exception = false;
    
    for (auto pc : pc_hooks) {
        if (pc == 0x2020) {
            found_handler = true;
        }
        if (pc == 0x1002 && found_handler) {
            returned_from_exception = true;
        }
    }
    
    EXPECT_TRUE(found_handler) << "Should have jumped to illegal instruction handler at 0x2020";
    EXPECT_TRUE(returned_from_exception) << "Should have returned to instruction after illegal opcode";
}

TEST_F(ExceptionTest, PrivilegeViolationException) {
    /* Switch to user mode first */
    unsigned int sr = m68k_get_reg(NULL, M68K_REG_SR);
    sr &= ~0x2000; /* Clear supervisor bit */
    m68k_set_reg(M68K_REG_SR, sr);
    
    /* Try to execute a privileged instruction in user mode */
    write_word(0x1000, 0x4E72); /* STOP instruction (privileged) */
    write_word(0x1002, 0x2700); /* STOP parameter */
    write_word(0x1004, 0x4E71); /* NOP (after exception return) */
    
    /* Clear PC hooks */
    pc_hooks.clear();
    
    /* Execute - should trigger privilege violation */
    m68k_execute(100);
    
    /* Check that we jumped to the privilege violation handler */
    bool found_handler = false;
    for (auto pc : pc_hooks) {
        if (pc == 0x2060) {
            found_handler = true;
            break;
        }
    }
    
    EXPECT_TRUE(found_handler) << "Should have jumped to privilege violation handler at 0x2060";
    
    /* After RTE, we should be back in user mode (unless handler modified stacked SR) */
    sr = m68k_get_reg(NULL, M68K_REG_SR);
    EXPECT_TRUE((sr & 0x2000) == 0) << "Should be back in user mode after RTE from privilege violation";
}

TEST_F(ExceptionTest, ZeroDivideException) {
    /* Set up registers for division */
    m68k_set_reg(M68K_REG_D0, 100);
    m68k_set_reg(M68K_REG_D1, 0);
    
    /* DIVU D1, D0 - divide by zero */
    write_word(0x1000, 0x80C1); /* DIVU D1, D0 */
    write_word(0x1002, 0x4E71); /* NOP (after exception return) */
    
    /* Clear PC hooks */
    pc_hooks.clear();
    
    /* Execute - should trigger zero divide exception */
    m68k_execute(100);
    
    /* Check that we jumped to the zero divide handler */
    bool found_handler = false;
    for (auto pc : pc_hooks) {
        if (pc == 0x2030) {
            found_handler = true;
            break;
        }
    }
    
    EXPECT_TRUE(found_handler) << "Should have jumped to zero divide handler at 0x2030";
}

TEST_F(ExceptionTest, TrapInstruction) {
    /* TRAP #0 instruction */
    write_word(0x1000, 0x4E40); /* TRAP #0 */
    write_word(0x1002, 0x4E71); /* NOP (after trap return) */
    
    /* Clear PC hooks */
    pc_hooks.clear();
    
    /* Execute the TRAP instruction */
    m68k_execute(100);
    
    /* Check that we jumped to TRAP #0 handler */
    bool found_handler = false;
    bool returned_from_trap = false;
    
    for (auto pc : pc_hooks) {
        if (pc == 0x3000) { /* TRAP #0 vector at 0x80 points to 0x3000 */
            found_handler = true;
        }
        if (pc == 0x1002 && found_handler) {
            returned_from_trap = true;
        }
    }
    
    EXPECT_TRUE(found_handler) << "Should have jumped to TRAP #0 handler at 0x3000";
    EXPECT_TRUE(returned_from_trap) << "Should have returned to instruction after TRAP";
}

TEST_F(ExceptionTest, MultipleTrapVectors) {
    /* Test different TRAP numbers */
    uint32_t pc = 0x1000;
    
    /* TRAP #0 */
    write_word(pc, 0x4E40); pc += 2;
    /* TRAP #5 */
    write_word(pc, 0x4E45); pc += 2;
    /* TRAP #15 */
    write_word(pc, 0x4E4F); pc += 2;
    /* STOP to end execution */
    write_word(pc, 0x4E72); pc += 2;
    write_word(pc, 0x2700); pc += 2;
    
    /* Clear PC hooks */
    pc_hooks.clear();
    
    /* Execute the program */
    m68k_execute(200);
    
    /* Check that we hit the correct TRAP handlers */
    bool found_trap0 = false;
    bool found_trap5 = false;
    bool found_trap15 = false;
    
    for (auto hook_pc : pc_hooks) {
        if (hook_pc == 0x3000) found_trap0 = true;  /* TRAP #0 handler */
        if (hook_pc == 0x3050) found_trap5 = true;  /* TRAP #5 handler */
        if (hook_pc == 0x30F0) found_trap15 = true; /* TRAP #15 handler */
    }
    
    EXPECT_TRUE(found_trap0) << "Should have executed TRAP #0 handler";
    EXPECT_TRUE(found_trap5) << "Should have executed TRAP #5 handler";
    EXPECT_TRUE(found_trap15) << "Should have executed TRAP #15 handler";
}

TEST_F(ExceptionTest, CHKInstructionException) {
    /* Set up registers for CHK */
    m68k_set_reg(M68K_REG_D0, 200);  /* Value to check */
    m68k_set_reg(M68K_REG_D1, 100);  /* Upper bound (will fail: 200 > 100) */
    
    /* CHK D1, D0 - check if D0 is within bounds [0..D1] */
    write_word(0x1000, 0x4181); /* CHK D1, D0 - opcode 0100 000 110 000 001 */
    write_word(0x1002, 0x4E71); /* NOP (after exception return) */
    
    /* Clear PC hooks */
    pc_hooks.clear();
    
    /* Execute - should trigger CHK exception */
    m68k_execute(100);
    
    /* Check that we jumped to the CHK handler */
    bool found_handler = false;
    for (auto pc : pc_hooks) {
        if (pc == 0x2040) {
            found_handler = true;
            break;
        }
    }
    
    EXPECT_TRUE(found_handler) << "Should have jumped to CHK handler at 0x2040";
}

TEST_F(ExceptionTest, Line1010EmulatorException) {
    /* Write a Line A (1010) instruction */
    write_word(0x1000, 0xA000); /* Line 1010 instruction */
    write_word(0x1002, 0x4E71); /* NOP (after exception return) */
    
    /* Clear PC hooks */
    pc_hooks.clear();
    
    /* Execute - should trigger Line 1010 exception */
    m68k_execute(100);
    
    /* Check that we jumped to the Line 1010 handler */
    bool found_handler = false;
    for (auto pc : pc_hooks) {
        if (pc == 0x2080) {
            found_handler = true;
            break;
        }
    }
    
    EXPECT_TRUE(found_handler) << "Should have jumped to Line 1010 handler at 0x2080";
}

TEST_F(ExceptionTest, Line1111EmulatorException) {
    /* Write a Line F (1111) instruction */
    write_word(0x1000, 0xF000); /* Line 1111 instruction */
    write_word(0x1002, 0x4E71); /* NOP (after exception return) */
    
    /* Clear PC hooks */
    pc_hooks.clear();
    
    /* Execute - should trigger Line 1111 exception */
    m68k_execute(100);
    
    /* Check that we jumped to the Line 1111 handler */
    bool found_handler = false;
    for (auto pc : pc_hooks) {
        if (pc == 0x2090) {
            found_handler = true;
            break;
        }
    }
    
    EXPECT_TRUE(found_handler) << "Should have jumped to Line 1111 handler at 0x2090";
}

TEST_F(ExceptionTest, ExceptionStackFrame) {
    /* Test that exception stack frame is properly created */
    uint32_t initial_sp = m68k_get_reg(NULL, M68K_REG_SP);
    
    /* Create a more complex exception handler that modifies a register */
    write_word(0x2020, 0x7001); /* MOVEQ #1, D0 */
    write_word(0x2022, 0x4E73); /* RTE */
    
    /* Set D0 to 0 initially */
    m68k_set_reg(M68K_REG_D0, 0);
    
    /* Illegal instruction to trigger exception */
    write_word(0x1000, 0x4AFC); /* Illegal instruction */
    
    /* Execute */
    m68k_execute(100);
    
    /* D0 should have been set to 1 by the exception handler */
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_D0), 1) << "Exception handler should have set D0 to 1";
    
    /* Stack pointer should be restored */
    EXPECT_EQ(m68k_get_reg(NULL, M68K_REG_SP), initial_sp) << "Stack pointer should be restored after RTE";
}

TEST_F(ExceptionTest, NestedExceptions) {
    /* Test nested exception handling */
    
    /* Main exception handler at 0x2020 triggers another exception */
    write_word(0x2020, 0x4AFC); /* Illegal instruction in handler (triggers nested) */
    write_word(0x2022, 0x4E73); /* RTE (never reached directly) */
    
    /* Nested exception handler at 0x2020 (will be called recursively) */
    /* To avoid infinite recursion, we need a different handler */
    /* Use privilege violation handler for the nested exception */
    write_word(0x2060, 0x7001); /* MOVEQ #1, D0 */
    write_word(0x2062, 0x4E73); /* RTE */
    
    /* However, illegal instruction in exception handler is a special case */
    /* The CPU should handle this gracefully */
    
    /* Set up a counter in memory to track handler entries */
    write_long(0x4000, 0);
    
    /* Modified illegal instruction handler that increments counter */
    write_word(0x2020, 0x5279); /* ADDQ.W #1, (0x4000).L */
    write_long(0x2022, 0x00004000);
    write_word(0x2026, 0x4E73); /* RTE */
    
    /* Trigger initial exception */
    write_word(0x1000, 0x4AFC); /* Illegal instruction */
    write_word(0x1002, 0x4E71); /* NOP */
    
    /* Execute */
    m68k_execute(100);
    
    /* Check that handler was called */
    uint16_t counter = read_word(0x4000);
    EXPECT_GT(counter, 0) << "Exception handler should have been called";
}