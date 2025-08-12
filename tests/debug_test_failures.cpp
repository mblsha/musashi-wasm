#include "m68k_test_common.h"
#include <cstdio>

extern "C" {
    unsigned int m68k_disassemble(char* str_buff, unsigned int pc, unsigned int cpu_type);
    void m68k_set_cpu_type(unsigned int cpu_type);
}

/* Debug test to collect information about failing tests */
DECLARE_M68K_TEST(DebugTest) {};

TEST_F(DebugTest, DebugSTOPInstruction) {
    printf("\n=== DEBUG: STOP Instruction Test ===\n");
    
    // Set CPU type explicitly
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    
    // Write STOP #$2000 at 0x400
    write_word(0x400, 0x4E72);  // STOP opcode
    write_word(0x402, 0x2000);  // Immediate value
    write_word(0x404, 0x4E75);  // RTS (for reference)
    
    // Check initial state
    uint32_t initial_pc = m68k_get_reg(NULL, M68K_REG_PC);
    uint32_t initial_sr = m68k_get_reg(NULL, M68K_REG_SR);
    
    printf("Before execution:\n");
    printf("  PC=0x%06X  SR=0x%04X\n", initial_pc, initial_sr);
    printf("  Supervisor bit (S): %s\n", (initial_sr & 0x2000) ? "1 (supervisor)" : "0 (user)");
    printf("  Interrupt mask: %d\n", (initial_sr >> 8) & 7);
    
    // Dump memory at PC
    printf("  Memory at PC: ");
    for (int i = 0; i < 6; i++) {
        printf("%02X ", memory[0x400 + i]);
    }
    printf("\n");
    
    // Disassemble
    char disasm_buf[256];
    m68k_disassemble(disasm_buf, 0x400, M68K_CPU_TYPE_68000);
    printf("  Disassembly: %s\n", disasm_buf);
    
    // Execute and check cycles
    printf("\nExecuting 100 cycles...\n");
    int cycles = m68k_execute(100);
    
    uint32_t after_pc = m68k_get_reg(NULL, M68K_REG_PC);
    uint32_t after_sr = m68k_get_reg(NULL, M68K_REG_SR);
    
    printf("After first execution:\n");
    printf("  Cycles returned: %d\n", cycles);
    printf("  PC=0x%06X  SR=0x%04X\n", after_pc, after_sr);
    printf("  SR changed: %s\n", (after_sr != initial_sr) ? "YES" : "NO");
    
    // Check if STOP advanced PC
    printf("  PC advanced past STOP: %s (should be 0x404 if executed)\n", 
           (after_pc == 0x404) ? "YES" : "NO");
    
    // Try another execution
    printf("\nTrying second execution (should return 0 if stopped)...\n");
    cycles = m68k_execute(100);
    printf("  Second execution cycles: %d\n", cycles);
    
    uint32_t third_pc = m68k_get_reg(NULL, M68K_REG_PC);
    printf("  PC after second execute: 0x%06X\n", third_pc);
    
    // Check for exception vectors
    printf("\nException vectors:\n");
    printf("  Privilege violation vector (8): 0x%08X\n", read_long(0x20));
    printf("  Illegal instruction vector (4): 0x%08X\n", read_long(0x10));
}

TEST_F(DebugTest, DebugADDInstruction) {
    printf("\n=== DEBUG: ADD Instruction Test ===\n");
    
    // Try both possible encodings for ADD.W #5, D0
    // 1. ADDI.W #imm, D0 (0x0640)
    write_word(0x400, 0x0640);
    write_word(0x402, 0x0005);
    
    // 2. Alternative: ORI.W #imm, D0 for comparison (0x0040)
    write_word(0x404, 0x0040);
    write_word(0x406, 0x0005);
    
    // 3. Correct ADDI.W #5, D0 encoding
    write_word(0x408, 0x0640);  // ADDI.W #imm, D0
    write_word(0x40A, 0x0005);
    
    // Disassemble all three
    char disasm_buf[256];
    printf("Disassembly at 0x400-0x40A:\n");
    
    m68k_disassemble(disasm_buf, 0x400, M68K_CPU_TYPE_68000);
    printf("  0x400: %s (bytes: %04X %04X)\n", disasm_buf, read_word(0x400), read_word(0x402));
    
    m68k_disassemble(disasm_buf, 0x404, M68K_CPU_TYPE_68000);
    printf("  0x404: %s (bytes: %04X %04X)\n", disasm_buf, read_word(0x404), read_word(0x406));
    
    m68k_disassemble(disasm_buf, 0x408, M68K_CPU_TYPE_68000);
    printf("  0x408: %s (bytes: %04X %04X)\n", disasm_buf, read_word(0x408), read_word(0x40A));
    
    // Test execution
    m68k_set_reg(M68K_REG_D0, 10);
    m68k_set_reg(M68K_REG_PC, 0x400);
    
    printf("\nBefore execution: PC=0x%06X, D0=%d\n", 
           m68k_get_reg(NULL, M68K_REG_PC),
           m68k_get_reg(NULL, M68K_REG_D0));
    
    m68k_execute(10);
    
    printf("After execution: PC=0x%06X, D0=%d\n",
           m68k_get_reg(NULL, M68K_REG_PC),
           m68k_get_reg(NULL, M68K_REG_D0));
}

TEST_F(DebugTest, DebugJMPandHook) {
    printf("\n=== DEBUG: JMP and PC Hook Test ===\n");
    
    // Write JMP to 0x500
    write_word(0x400, 0x4E71);  // NOP first
    write_word(0x402, 0x4EF9);  // JMP (xxx).L
    write_long(0x404, 0x00000500);
    write_word(0x500, 0x4E71);  // NOP at target
    
    // Disassemble
    char disasm_buf[256];
    m68k_disassemble(disasm_buf, 0x400, M68K_CPU_TYPE_68000);
    printf("  0x400: %s\n", disasm_buf);
    m68k_disassemble(disasm_buf, 0x402, M68K_CPU_TYPE_68000);
    printf("  0x402: %s\n", disasm_buf);
    
    printf("\nPC hooks collected:\n");
    m68k_execute(20);
    
    for (size_t i = 0; i < pc_hooks.size() && i < 10; i++) {
        printf("  Hook %zu: PC=0x%06X\n", i, pc_hooks[i]);
    }
    
    if (std::find(pc_hooks.begin(), pc_hooks.end(), 0x500) != pc_hooks.end()) {
        printf("  ✓ PC 0x500 was reached\n");
    } else {
        printf("  ✗ PC 0x500 was NOT reached\n");
    }
}

TEST_F(DebugTest, DebugCompareMnemonics) {
    printf("\n=== DEBUG: CMP Instruction Mnemonics ===\n");
    
    // Write various compare instructions
    write_word(0x400, 0xB040);  // CMP.W D0, D0
    write_word(0x402, 0x0C40);  // CMPI.W #imm, D0
    write_word(0x404, 0x0005);  // immediate value
    write_word(0x406, 0xB0C0);  // CMPA.W D0, A0
    
    // Disassemble and check mnemonics
    char disasm_buf[256];
    
    m68k_disassemble(disasm_buf, 0x400, M68K_CPU_TYPE_68000);
    printf("  0x400: '%s'\n", disasm_buf);
    
    // Parse mnemonic
    std::string full_disasm = disasm_buf;
    size_t space_pos = full_disasm.find_first_of(" \t");
    std::string mnemonic = (space_pos != std::string::npos) ? 
                           full_disasm.substr(0, space_pos) : full_disasm;
    printf("  Extracted mnemonic: '%s'\n", mnemonic.c_str());
    
    // Check variations
    printf("  mnemonic == \"cmp\": %s\n", (mnemonic == "cmp") ? "TRUE" : "FALSE");
    printf("  mnemonic == \"cmp.w\": %s\n", (mnemonic == "cmp.w") ? "TRUE" : "FALSE");
    
    // Try lowercase comparison
    std::string lower_mnemonic = mnemonic;
    std::transform(lower_mnemonic.begin(), lower_mnemonic.end(), 
                  lower_mnemonic.begin(), ::tolower);
    printf("  Lower mnemonic: '%s'\n", lower_mnemonic.c_str());
    printf("  lower == \"cmp\": %s\n", (lower_mnemonic == "cmp") ? "TRUE" : "FALSE");
    printf("  lower == \"cmp.w\": %s\n", (lower_mnemonic == "cmp.w") ? "TRUE" : "FALSE");
    
    // Check CMPI
    m68k_disassemble(disasm_buf, 0x402, M68K_CPU_TYPE_68000);
    printf("\n  0x402: '%s'\n", disasm_buf);
    full_disasm = disasm_buf;
    space_pos = full_disasm.find_first_of(" \t");
    mnemonic = (space_pos != std::string::npos) ? 
               full_disasm.substr(0, space_pos) : full_disasm;
    printf("  Extracted mnemonic: '%s'\n", mnemonic.c_str());
    
    // Check CMPA
    m68k_disassemble(disasm_buf, 0x406, M68K_CPU_TYPE_68000);
    printf("\n  0x406: '%s'\n", disasm_buf);
    full_disasm = disasm_buf;
    space_pos = full_disasm.find_first_of(" \t");
    mnemonic = (space_pos != std::string::npos) ? 
               full_disasm.substr(0, space_pos) : full_disasm;
    printf("  Extracted mnemonic: '%s'\n", mnemonic.c_str());
}