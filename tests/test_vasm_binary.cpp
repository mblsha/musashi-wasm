/* Test that loads and validates a binary assembled with vasm */

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <fstream>
#include "m68k.h"
#include "m68k_perfetto.h"
#include "m68ktrace.h"

extern "C" {
    void set_read_mem_func(int (*func)(unsigned int address, int size));
    void set_write_mem_func(void (*func)(unsigned int address, int size, unsigned int value));
    void set_pc_hook_func(int (*func)(unsigned int pc));
    
    int perfetto_init(const char* process_name);
    void perfetto_destroy(void);
    void perfetto_enable_flow(int enable);
    void perfetto_enable_memory(int enable);
    void perfetto_enable_instructions(int enable);
    int perfetto_save_trace(const char* filename);
    int perfetto_is_initialized(void);
}

class VasmBinaryTest : public ::testing::Test {
protected:
    std::vector<uint8_t> memory;
    std::vector<unsigned int> pc_hooks;
    
    static VasmBinaryTest* instance;
    
    void SetUp() override {
        instance = this;
        memory.resize(1024 * 1024, 0);
        pc_hooks.clear();
        
        /* Set up memory callbacks */
        set_read_mem_func(read_memory_static);
        set_write_mem_func(write_memory_static);
        set_pc_hook_func(pc_hook_static);
        
        /* Initialize M68K */
        m68k_init();
        
        /* Set up reset vector */
        write_long(0, 0x1000);  /* Initial SP */
        write_long(4, 0x400);   /* Initial PC */
        
        /* Reset CPU */
        m68k_pulse_reset();
        
        /* Dummy execution to handle initialization overhead */
        m68k_execute(1);
    }
    
    void TearDown() override {
        instance = nullptr;
    }
    
    bool LoadBinaryFile(const std::string& filename, uint32_t load_address) {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file) {
            return false;
        }
        
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        if (load_address + size > memory.size()) {
            return false;
        }
        
        file.read(reinterpret_cast<char*>(&memory[load_address]), size);
        return file.good();
    }
    
    void write_long(uint32_t addr, uint32_t value) {
        memory[addr] = (value >> 24) & 0xFF;
        memory[addr + 1] = (value >> 16) & 0xFF;
        memory[addr + 2] = (value >> 8) & 0xFF;
        memory[addr + 3] = value & 0xFF;
    }
    
    uint32_t read_long(uint32_t addr) {
        return (memory[addr] << 24) |
               (memory[addr + 1] << 16) |
               (memory[addr + 2] << 8) |
               memory[addr + 3];
    }
    
    uint16_t read_word(uint32_t addr) {
        return (memory[addr] << 8) | memory[addr + 1];
    }
    
private:
    static int read_memory_static(unsigned int address, int size) {
        return instance ? instance->read_memory(address, size) : 0;
    }
    
    static void write_memory_static(unsigned int address, int size, unsigned int value) {
        if (instance) instance->write_memory(address, size, value);
    }
    
    static int pc_hook_static(unsigned int pc) {
        return instance ? instance->pc_hook(pc) : 0;
    }
    
    int read_memory(unsigned int address, int size) {
        if (address >= memory.size()) return 0;
        
        switch (size) {
            case 1:
                return memory[address];
            case 2:
                if (address + 1 < memory.size()) {
                    return (memory[address] << 8) | memory[address + 1];
                }
                break;
            case 4:
                if (address + 3 < memory.size()) {
                    return (memory[address] << 24) | 
                           (memory[address + 1] << 16) |
                           (memory[address + 2] << 8) | 
                           memory[address + 3];
                }
                break;
        }
        return 0;
    }
    
    void write_memory(unsigned int address, int size, unsigned int value) {
        if (address >= memory.size()) return;
        
        switch (size) {
            case 1:
                memory[address] = value & 0xFF;
                break;
            case 2:
                if (address + 1 < memory.size()) {
                    memory[address] = (value >> 8) & 0xFF;
                    memory[address + 1] = value & 0xFF;
                }
                break;
            case 4:
                if (address + 3 < memory.size()) {
                    memory[address] = (value >> 24) & 0xFF;
                    memory[address + 1] = (value >> 16) & 0xFF;
                    memory[address + 2] = (value >> 8) & 0xFF;
                    memory[address + 3] = value & 0xFF;
                }
                break;
        }
    }
    
    int pc_hook(unsigned int pc) {
        pc_hooks.push_back(pc);
        return 0; /* Continue execution */
    }
};

VasmBinaryTest* VasmBinaryTest::instance = nullptr;

TEST_F(VasmBinaryTest, LoadAndValidateBinary) {
    /* Load the assembled binary */
    ASSERT_TRUE(LoadBinaryFile("tests/test_program.bin", 0x400));
    
    /* Verify the binary was loaded correctly by checking first few instructions */
    EXPECT_EQ(0x303C, read_word(0x400));  /* MOVE.W #5,D0 */
    EXPECT_EQ(0x0005, read_word(0x402));
    EXPECT_EQ(0x611C, read_word(0x404));  /* BSR factorial */
    
    /* Verify data section */
    EXPECT_EQ(0x0008, read_word(0x484));  /* First array element */
    EXPECT_EQ(0x0003, read_word(0x486));  /* Second array element */
}

TEST_F(VasmBinaryTest, ExecuteBinaryWithPerfettoTrace) {
    /* Load the assembled binary */
    ASSERT_TRUE(LoadBinaryFile("tests/test_program.bin", 0x400));
    
    /* Enable M68K tracing */
    m68k_trace_enable(1);
    
    /* Initialize Perfetto */
    if (perfetto_init("VasmBinary") == 0) {
        perfetto_enable_flow(1);
        perfetto_enable_memory(0);      /* Disable memory tracing for cleaner output */
        perfetto_enable_instructions(0); /* Disable instruction tracing */
    }
    
    /* Execute the program */
    int total_cycles = 0;
    int iterations = 0;
    const int MAX_ITERATIONS = 1000;
    
    while (iterations < MAX_ITERATIONS) {
        uint32_t pc_before = m68k_get_reg(NULL, M68K_REG_PC);
        int cycles = m68k_execute(100);
        total_cycles += cycles;
        iterations++;
        
        /* Check if stopped */
        if (cycles == 0) {
            break;
        }
        
        /* Safety check */
        uint32_t current_pc = m68k_get_reg(NULL, M68K_REG_PC);
        if (current_pc < 0x400 || current_pc > 0x500) {
            break;
        }
    }
    
    /* Verify execution reached key functions */
    bool found_factorial = false;
    bool found_fibonacci = false;
    bool found_bubble_sort = false;
    
    for (auto pc : pc_hooks) {
        if (pc == 0x41C) found_factorial = true;    /* factorial function */
        if (pc == 0x434) found_fibonacci = true;    /* fibonacci function */
        if (pc == 0x454) found_bubble_sort = true;  /* bubble_sort function */
    }
    
    EXPECT_TRUE(found_factorial) << "Factorial function was not called";
    EXPECT_TRUE(found_fibonacci) << "Fibonacci function was not called";
    EXPECT_TRUE(found_bubble_sort) << "Bubble sort function was not called";
    
    /* Verify results were written to memory */
    uint32_t factorial_result = read_long(0x494);  /* result1 */
    uint32_t fibonacci_result = read_long(0x498);  /* result2 */
    
    EXPECT_EQ(120, factorial_result) << "Factorial(5) should be 120";
    EXPECT_EQ(2, fibonacci_result) << "Fibonacci(3) should be 2";
    
    /* Verify array was sorted */
    uint16_t prev = read_word(0x484);
    for (int i = 1; i < 8; i++) {
        uint16_t curr = read_word(0x484 + i * 2);
        EXPECT_LE(prev, curr) << "Array not sorted at index " << i;
        prev = curr;
    }
    
    /* Save Perfetto trace if initialized */
    if (perfetto_is_initialized()) {
        perfetto_save_trace("vasm_binary_trace.perfetto-trace");
        perfetto_destroy();
    }
}

TEST_F(VasmBinaryTest, ValidateInstructionEncoding) {
    /* Load the assembled binary */
    ASSERT_TRUE(LoadBinaryFile("tests/test_program.bin", 0x400));
    
    /* Validate specific instruction encodings */
    struct InstructionCheck {
        uint32_t address;
        uint16_t opcode;
        const char* description;
    };
    
    InstructionCheck checks[] = {
        {0x400, 0x303C, "MOVE.W #imm,D0"},
        {0x404, 0x611C, "BSR factorial"},
        {0x408, 0x21C0, "MOVE.L D0,abs.l"},
        {0x418, 0x4E72, "STOP #imm"},
        {0x41C, 0xB07C, "CMP.W #imm,D0"},
        {0x432, 0x4E75, "RTS"},
        {0x454, 0x48E7, "MOVEM.L regs,-(SP)"},
    };
    
    for (const auto& check : checks) {
        uint16_t opcode = read_word(check.address);
        EXPECT_EQ(check.opcode, opcode) 
            << "Incorrect opcode at 0x" << std::hex << check.address 
            << " for " << check.description;
    }
}