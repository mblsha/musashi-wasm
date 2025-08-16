/* Refactored myfunc API tests - uses base class with extensions for API-specific features */

#include "m68k_test_common.h"

extern "C" {
    // Functions from myfunc.cc
    int my_initialize();
    void enable_printf_logging();
    void add_pc_hook_addr(unsigned int addr);
    void add_region(unsigned int start, unsigned int size, void* data);
    void clear_regions();
    
    // Memory access functions
    unsigned int m68k_read_memory_8(unsigned int address);
    unsigned int m68k_read_memory_16(unsigned int address);
    unsigned int m68k_read_memory_32(unsigned int address);
    void m68k_write_memory_8(unsigned int address, unsigned int value);
    void m68k_write_memory_16(unsigned int address, unsigned int value);
    void m68k_write_memory_32(unsigned int address, unsigned int value);
}

/* Extended test class for myfunc API with write logging */
class MyFuncTest : public M68kMinimalTestBase<MyFuncTest> {
protected:
    std::vector<std::pair<unsigned int, unsigned int>> write_log;
    
    // Override to add write logging
    void OnSetUp() override {
        write_log.clear();
    }
    
    void OnTearDown() override {
        clear_regions();  // Clear all memory regions between tests
    }
    
    // Custom write callback that logs writes
    void LogWrite(unsigned int address, unsigned int value) {
        write_log.push_back({address, value});
    }
};

// Test initialization
TEST_F(MyFuncTest, Initialization) {
    // First call should return 0 (not initialized)
    EXPECT_EQ(my_initialize(), 0);
    
    // Second call should return 1 (already initialized)
    EXPECT_EQ(my_initialize(), 1);
}

// Test memory regions
TEST_F(MyFuncTest, MemoryRegions) {
    // Create a test region
    uint8_t* region_data = new uint8_t[256];
    for (int i = 0; i < 256; i++) {
        region_data[i] = i;
    }
    
    // Add region at address 0x1000
    add_region(0x1000, 256, region_data);
    
    // Test reading from the region
    EXPECT_EQ(m68k_read_memory_8(0x1000), 0);
    EXPECT_EQ(m68k_read_memory_8(0x1010), 0x10);
    EXPECT_EQ(m68k_read_memory_8(0x10FF), 0xFF);
    
    // Test 16-bit reads
    EXPECT_EQ(m68k_read_memory_16(0x1000), 0x0001);
    EXPECT_EQ(m68k_read_memory_16(0x1010), 0x1011);
    
    // Test 32-bit reads
    EXPECT_EQ(m68k_read_memory_32(0x1000), 0x00010203);
    EXPECT_EQ(m68k_read_memory_32(0x1010), 0x10111213);
    
    // Clean up allocated memory
    delete[] region_data;
}

// Test memory callbacks
TEST_F(MyFuncTest, MemoryCallbacks) {
    // Write test data to our test memory (using base class memory)
    memory[0x100] = 0xAB;
    memory[0x101] = 0xCD;
    memory[0x102] = 0xEF;
    memory[0x103] = 0x12;
    
    // Test reading through callbacks
    EXPECT_EQ(m68k_read_memory_8(0x100), 0xAB);
    EXPECT_EQ(m68k_read_memory_16(0x100), 0xABCD);
    EXPECT_EQ(m68k_read_memory_32(0x100), 0xABCDEF12);
    
    // Test writing through callbacks
    m68k_write_memory_8(0x200, 0x55);
    m68k_write_memory_16(0x202, 0xAABB);
    m68k_write_memory_32(0x204, 0x11223344);
    
    EXPECT_EQ(memory[0x200], 0x55);
    EXPECT_EQ(read_word(0x202), 0xAABB);
    EXPECT_EQ(read_long(0x204), 0x11223344);
}

// Test PC hook addresses
TEST_F(MyFuncTest, PCHookAddresses) {
    // Add specific addresses to hook
    add_pc_hook_addr(0x1000);
    add_pc_hook_addr(0x1010);
    add_pc_hook_addr(0x1020);
    
    // Write NOPs at those addresses
    write_word(0x1000, 0x4E71);
    write_word(0x1010, 0x4E71);
    write_word(0x1020, 0x4E71);
    
    // Write BRA to jump between them
    write_word(0x1002, 0x6000); // BRA.w
    write_word(0x1004, 0x0008); // to 0x1010 (base 0x1008, disp 0x0008)
    
    write_word(0x1012, 0x6000); // BRA.w
    write_word(0x1014, 0x0008); // to 0x1020 (base 0x1018, disp 0x0008)
    
    // Set PC to start at 0x1000
    write_long(4, 0x1000);
    m68k_pulse_reset();
    
    // Execute and verify hooks were called
    pc_hooks.clear();
    m68k_execute(100);
    
    // Should have hooked all three addresses
    bool found_1000 = false, found_1010 = false, found_1020 = false;
    for (auto pc : pc_hooks) {
        if (pc == 0x1000) found_1000 = true;
        if (pc == 0x1010) found_1010 = true;
        if (pc == 0x1020) found_1020 = true;
    }
    
    EXPECT_TRUE(found_1000) << "PC hook at 0x1000 not triggered";
    EXPECT_TRUE(found_1010) << "PC hook at 0x1010 not triggered";
    EXPECT_TRUE(found_1020) << "PC hook at 0x1020 not triggered";
}

// Test mixed memory regions and callbacks
TEST_F(MyFuncTest, MixedMemoryAccess) {
    // Create a region with specific data
    uint8_t* region = new uint8_t[16];
    for (int i = 0; i < 16; i++) {
        region[i] = 0xF0 + i;
    }
    add_region(0x2000, 16, region);
    
    // Write data to callback memory
    write_long(0x3000, 0xDEADBEEF);
    
    // Test reading from region
    EXPECT_EQ(m68k_read_memory_8(0x2000), 0xF0);
    EXPECT_EQ(m68k_read_memory_8(0x200F), 0xFF);
    
    // Test reading from callback memory
    EXPECT_EQ(m68k_read_memory_32(0x3000), 0xDEADBEEF);
    
    // Test writing to region (should modify the region data)
    m68k_write_memory_8(0x2000, 0xAA);
    EXPECT_EQ(region[0], 0xAA);
    
    // Test writing to callback memory
    m68k_write_memory_16(0x3004, 0x1234);
    EXPECT_EQ(read_word(0x3004), 0x1234);
    
    delete[] region;
}

// Test region overlap and priority
TEST_F(MyFuncTest, RegionPriority) {
    // Create two regions that might overlap logically
    uint8_t* region1 = new uint8_t[256];
    uint8_t* region2 = new uint8_t[256];
    
    std::fill(region1, region1 + 256, 0x11);
    std::fill(region2, region2 + 256, 0x22);
    
    // Add regions at different addresses
    add_region(0x1000, 256, region1);
    add_region(0x2000, 256, region2);
    
    // Verify each region is independent
    EXPECT_EQ(m68k_read_memory_8(0x1000), 0x11);
    EXPECT_EQ(m68k_read_memory_8(0x2000), 0x22);
    
    // Write to each region
    m68k_write_memory_8(0x1000, 0xAA);
    m68k_write_memory_8(0x2000, 0xBB);
    
    EXPECT_EQ(region1[0], 0xAA);
    EXPECT_EQ(region2[0], 0xBB);
    
    delete[] region1;
    delete[] region2;
}

// Test clear_regions functionality
TEST_F(MyFuncTest, ClearRegions) {
    // Create and add a region
    uint8_t* region = new uint8_t[128];
    std::fill(region, region + 128, 0x55);
    add_region(0x5000, 128, region);
    
    // Verify region is active
    EXPECT_EQ(m68k_read_memory_8(0x5000), 0x55);
    
    // Clear all regions
    clear_regions();
    
    // After clearing, reads should go to callback memory (which is 0)
    EXPECT_EQ(m68k_read_memory_8(0x5000), 0);
    
    // Clean up - note: clear_regions doesn't free memory, just removes references
    delete[] region;
}

// Test execution with regions
TEST_F(MyFuncTest, ExecutionWithRegions) {
    // Create a code region
    uint8_t* code = new uint8_t[32];
    memset(code, 0, 32);  // Clear memory first
    uint16_t* code16 = reinterpret_cast<uint16_t*>(code);
    
    // Debug: Show what we're writing
    fprintf(stderr, "\n=== ExecutionWithRegions Debug Info ===\n");
    fprintf(stderr, "Code buffer address: %p\n", (void*)code);
    
    // Write NOP instructions
    code16[0] = __builtin_bswap16(0x4E71); // NOP
    code16[1] = __builtin_bswap16(0x4E71); // NOP
    code16[2] = __builtin_bswap16(0x303C); // MOVE.W #$1234,D0
    code16[3] = __builtin_bswap16(0x1234);
    code16[4] = __builtin_bswap16(0x4E71); // NOP
    
    // Debug: Verify what was written to the buffer
    fprintf(stderr, "Code bytes written to buffer:\n");
    for (int i = 0; i < 12; i++) {
        fprintf(stderr, "  [%02d]: 0x%02X\n", i, code[i]);
    }
    
    // Add as region at 0x6000
    fprintf(stderr, "Adding region at 0x6000, size 32\n");
    add_region(0x6000, 32, code);
    
    // Debug: Verify region is readable
    fprintf(stderr, "Reading back from region at 0x6000:\n");
    for (unsigned int i = 0; i < 12; i++) {
        unsigned int byte = m68k_read_memory_8(0x6000 + i);
        fprintf(stderr, "  [0x%04X]: 0x%02X\n", 0x6000 + i, byte);
    }
    
    // Set PC to execute from region
    write_long(4, 0x6000);
    
    // Debug: Check reset vector
    unsigned int reset_pc = read_long(4);
    fprintf(stderr, "Reset vector at 0x0004: 0x%08X\n", reset_pc);
    
    m68k_pulse_reset();
    
    // Check what PC actually is after reset
    unsigned int pc_after_reset = m68k_get_reg(NULL, M68K_REG_PC);
    fprintf(stderr, "PC after reset: 0x%06X (expected 0x6000)\n", pc_after_reset);
    
    // Debug: Check initial D0 value
    unsigned int d0_before = m68k_get_reg(NULL, M68K_REG_D0);
    fprintf(stderr, "D0 before execution: 0x%08X\n", d0_before);
    
    // Execute - give it more cycles as expert suggested
    fprintf(stderr, "About to call m68k_execute(200)\n");
    int cycles = m68k_execute(200);
    fprintf(stderr, "m68k_execute returned %d cycles\n", cycles);
    
    // Debug: Check PC after execution
    unsigned int pc_after_exec = m68k_get_reg(NULL, M68K_REG_PC);
    fprintf(stderr, "PC after execution: 0x%06X\n", pc_after_exec);
    
    // Debug: Disassemble what was actually executed
    char disasm_buf[256];
    m68k_disassemble(disasm_buf, 0x6000, M68K_CPU_TYPE_68000);
    fprintf(stderr, "Instruction at 0x6000: %s\n", disasm_buf);
    m68k_disassemble(disasm_buf, 0x6002, M68K_CPU_TYPE_68000);
    fprintf(stderr, "Instruction at 0x6002: %s\n", disasm_buf);
    m68k_disassemble(disasm_buf, 0x6004, M68K_CPU_TYPE_68000);
    fprintf(stderr, "Instruction at 0x6004: %s\n", disasm_buf);
    
    // Verify D0 was set
    unsigned int d0_value = m68k_get_reg(NULL, M68K_REG_D0);
    fprintf(stderr, "D0 after execution: 0x%08X (expected 0x1234)\n", d0_value);
    
    // Debug: If D0 is wrong, show what it might be
    if ((d0_value & 0xFFFF) != 0x1234) {
        fprintf(stderr, "ERROR: D0 is 0x%04X (%d decimal, '%c' ASCII)\n", 
                d0_value & 0xFFFF, d0_value & 0xFFFF, 
                (d0_value & 0xFF) >= 32 && (d0_value & 0xFF) < 127 ? (d0_value & 0xFF) : '?');
    }
    
    fprintf(stderr, "=== End Debug Info ===\n\n");
    
    EXPECT_EQ(d0_value & 0xFFFF, 0x1234);  // Check only lower 16 bits since we used MOVE.W
    
    delete[] code;
}