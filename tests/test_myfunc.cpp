#include <gtest/gtest.h>
#include <cstring>
#include <vector>

extern "C" {
#include "m68k.h"

// Functions from myfunc.c
int my_initialize();
void enable_printf_logging();
void set_read_mem_func(int (*func)(unsigned int address, int size));
void set_write_mem_func(void (*func)(unsigned int address, int size, unsigned int value));
void set_pc_hook_func(int (*func)(unsigned int pc));
void add_pc_hook_addr(unsigned int addr);
void add_region(unsigned int start, unsigned int size, void* data);

// Memory access functions
unsigned int m68k_read_memory_8(unsigned int address);
unsigned int m68k_read_memory_16(unsigned int address);
unsigned int m68k_read_memory_32(unsigned int address);
void m68k_write_memory_8(unsigned int address, unsigned int value);
void m68k_write_memory_16(unsigned int address, unsigned int value);
void m68k_write_memory_32(unsigned int address, unsigned int value);
}

// Test fixture for myfunc API tests
class MyFuncTest : public ::testing::Test {
protected:
    // Memory storage for tests
    std::vector<uint8_t> test_memory;
    std::vector<std::pair<unsigned int, unsigned int>> write_log;
    std::vector<unsigned int> pc_hook_log;
    
    // Callback functions
    static MyFuncTest* instance;
    
    static int read_mem_callback(unsigned int address, int size) {
        if (!instance || address >= instance->test_memory.size()) {
            return 0;
        }
        
        if (size == 1) {
            return instance->test_memory[address];
        } else if (size == 2 && address + 1 < instance->test_memory.size()) {
            return (instance->test_memory[address] << 8) | 
                   instance->test_memory[address + 1];
        } else if (size == 4 && address + 3 < instance->test_memory.size()) {
            return (instance->test_memory[address] << 24) |
                   (instance->test_memory[address + 1] << 16) |
                   (instance->test_memory[address + 2] << 8) |
                   instance->test_memory[address + 3];
        }
        return 0;
    }
    
    static void write_mem_callback(unsigned int address, int size, unsigned int value) {
        if (!instance) return;
        instance->write_log.push_back({address, value});
        
        if (address < instance->test_memory.size()) {
            if (size == 1) {
                instance->test_memory[address] = value & 0xFF;
            } else if (size == 2 && address + 1 < instance->test_memory.size()) {
                instance->test_memory[address] = (value >> 8) & 0xFF;
                instance->test_memory[address + 1] = value & 0xFF;
            } else if (size == 4 && address + 3 < instance->test_memory.size()) {
                instance->test_memory[address] = (value >> 24) & 0xFF;
                instance->test_memory[address + 1] = (value >> 16) & 0xFF;
                instance->test_memory[address + 2] = (value >> 8) & 0xFF;
                instance->test_memory[address + 3] = value & 0xFF;
            }
        }
    }
    
    static int pc_hook_callback(unsigned int pc) {
        if (!instance) return 0;
        instance->pc_hook_log.push_back(pc);
        return 0;
    }
    
    void SetUp() override {
        instance = this;
        test_memory.resize(1024 * 1024); // 1MB test memory
        write_log.clear();
        pc_hook_log.clear();
        
        // Initialize the API
        set_read_mem_func(read_mem_callback);
        set_write_mem_func(write_mem_callback);
        set_pc_hook_func(pc_hook_callback);
    }
    
    void TearDown() override {
        instance = nullptr;
    }
};

MyFuncTest* MyFuncTest::instance = nullptr;

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
}

// Test memory callbacks
TEST_F(MyFuncTest, MemoryCallbacks) {
    // Write test data to our test memory
    test_memory[0x100] = 0xAB;
    test_memory[0x101] = 0xCD;
    test_memory[0x102] = 0xEF;
    test_memory[0x103] = 0x12;
    
    // Test reading through callbacks
    EXPECT_EQ(m68k_read_memory_8(0x100), 0xAB);
    EXPECT_EQ(m68k_read_memory_16(0x100), 0xABCD);
    EXPECT_EQ(m68k_read_memory_32(0x100), 0xABCDEF12);
    
    // Test writing through callbacks
    m68k_write_memory_8(0x200, 0x55);
    EXPECT_EQ(write_log.size(), 1);
    EXPECT_EQ(write_log[0].first, 0x200);
    EXPECT_EQ(write_log[0].second, 0x55);
    EXPECT_EQ(test_memory[0x200], 0x55);
    
    m68k_write_memory_16(0x202, 0x1234);
    EXPECT_EQ(write_log.size(), 2);
    EXPECT_EQ(test_memory[0x202], 0x12);
    EXPECT_EQ(test_memory[0x203], 0x34);
    
    m68k_write_memory_32(0x204, 0xDEADBEEF);
    EXPECT_EQ(write_log.size(), 3);
    EXPECT_EQ(test_memory[0x204], 0xDE);
    EXPECT_EQ(test_memory[0x205], 0xAD);
    EXPECT_EQ(test_memory[0x206], 0xBE);
    EXPECT_EQ(test_memory[0x207], 0xEF);
}

// Test PC hook functionality
TEST_F(MyFuncTest, PCHookAddresses) {
    // Add some PC hook addresses
    add_pc_hook_addr(0x1000);
    add_pc_hook_addr(0x2000);
    add_pc_hook_addr(0x3000);
    
    // Note: We can't easily test the actual hooking without running
    // the CPU, but we can verify the functions don't crash
    SUCCEED();
}

// Test logging functionality
TEST_F(MyFuncTest, LoggingEnable) {
    // This should not crash
    enable_printf_logging();
    
    // Try setting callbacks with logging enabled
    set_read_mem_func(read_mem_callback);
    set_write_mem_func(write_mem_callback);
    set_pc_hook_func(pc_hook_callback);
    
    SUCCEED();
}

// Test mixed regions and callbacks
TEST_F(MyFuncTest, MixedMemoryAccess) {
    // Set up a region for low memory
    uint8_t* low_mem = new uint8_t[256];
    for (int i = 0; i < 256; i++) {
        low_mem[i] = i;
    }
    add_region(0x0, 256, low_mem);
    
    // Set up test memory for high memory (through callbacks)
    test_memory[0x1000] = 0xAA;
    test_memory[0x1001] = 0xBB;
    
    // Test reading from region
    EXPECT_EQ(m68k_read_memory_8(0x10), 0x10);
    
    // Test reading from callback memory
    EXPECT_EQ(m68k_read_memory_8(0x1000), 0xAA);
    
    // Test writing (always goes through callback)
    m68k_write_memory_8(0x10, 0xFF);
    EXPECT_EQ(write_log.size(), 1);
    EXPECT_EQ(write_log[0].first, 0x10);
    EXPECT_EQ(write_log[0].second, 0xFF);
}