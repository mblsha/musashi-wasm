/* Minimal base class for ALL M68K tests - eliminates massive duplication */

#ifndef M68K_TEST_COMMON_H
#define M68K_TEST_COMMON_H

#include <gtest/gtest.h>
#include <cstring>
#include <cstdint>
#include <vector>
#include <fstream>
#include <string>
#include <utility>
#include "m68k.h"

extern "C" {
    void set_read8_callback(int32_t fp);
    void set_write8_callback(int32_t fp);
    void set_probe_callback(int32_t fp);
    void clear_pc_hook_addrs();
    void reset_myfunc_state();
    void clear_regions();
    void add_region(unsigned int start, unsigned int size, void* data);
}

/* Minimal base class with just memory management - no tracing overhead */
template<typename Derived>
class M68kMinimalTestBase : public ::testing::Test {
public:
    static Derived* instance;
    
    /* Template method pattern - derived classes can override */
    virtual void OnSetUp() {}
    virtual void OnTearDown() {}
    virtual int OnPcHook(unsigned int pc) { 
        pc_hooks.push_back(pc);
        return 0; // Continue by default
    }
    
protected:
    std::vector<uint8_t> memory;
    std::vector<unsigned int> pc_hooks;
    
    void SetUp() override {
        instance = static_cast<Derived*>(this);
        memory.resize(1024 * 1024, 0);
        memset(memory.data(), 0, memory.size());
        pc_hooks.clear();
        
        // Reset ALL myfunc state to ensure clean test isolation
        reset_myfunc_state();
        clear_pc_hook_addrs();
        clear_regions();
        
        // Initialize M68K FIRST (it resets all callbacks)
        m68k_init();
        
        // Set our callbacks (don't use regions - let callbacks handle everything)
        set_read8_callback(reinterpret_cast<int32_t>(read8_static));
        set_write8_callback(reinterpret_cast<int32_t>(write8_static));
        set_probe_callback(reinterpret_cast<int32_t>(pc_hook_static));
        
        write_long(0, 0x1000);  /* Initial SP */
        write_long(4, 0x400);   /* Initial PC */
        
        m68k_pulse_reset();
        
        OnSetUp(); // Allow derived classes to modify setup AFTER reset
        // This allows tests to override PC or other registers as needed
    }
    
    void TearDown() override {
        OnTearDown(); // Allow derived classes to add teardown
        instance = nullptr;
    }
    
    /* Memory utilities - used by EVERY test */
    void write_word(uint32_t addr, uint16_t value) {
        memory[addr] = (value >> 8) & 0xFF;
        memory[addr + 1] = value & 0xFF;
    }
    
    void write_long(uint32_t addr, uint32_t value) {
        memory[addr] = (value >> 24) & 0xFF;
        memory[addr + 1] = (value >> 16) & 0xFF;
        memory[addr + 2] = (value >> 8) & 0xFF;
        memory[addr + 3] = value & 0xFF;
    }
    
    uint16_t read_word(uint32_t addr) {
        return (memory[addr] << 8) | memory[addr + 1];
    }
    
    uint32_t read_long(uint32_t addr) {
        return (memory[addr] << 24) |
               (memory[addr + 1] << 16) |
               (memory[addr + 2] << 8) |
               memory[addr + 3];
    }
    
    bool LoadBinaryFile(const std::string& filename, uint32_t load_address) {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file) return false;
        
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        if (load_address + size > memory.size()) return false;
        
        file.read(reinterpret_cast<char*>(&memory[load_address]), size);
        return file.good();
    }
    
    /* Public for disassembler access */
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

private:
    static int read_memory_static(unsigned int address, int size) {
        return instance ? instance->read_memory(address, size) : 0;
    }
    
    static void write_memory_static(unsigned int address, int size, unsigned int value) {
        if (!instance || address >= instance->memory.size()) return;
        
        switch (size) {
            case 1:
                instance->memory[address] = value & 0xFF;
                break;
            case 2:
                if (address + 1 < instance->memory.size()) {
                    instance->memory[address] = (value >> 8) & 0xFF;
                    instance->memory[address + 1] = value & 0xFF;
                }
                break;
            case 4:
                if (address + 3 < instance->memory.size()) {
                    instance->memory[address] = (value >> 24) & 0xFF;
                    instance->memory[address + 1] = (value >> 16) & 0xFF;
                    instance->memory[address + 2] = (value >> 8) & 0xFF;
                    instance->memory[address + 3] = value & 0xFF;
                }
                break;
        }
    }

    static uint8_t read8_static(uint32_t address) {
        if (!instance || address >= instance->memory.size()) return 0;
        return instance->memory[address];
    }

    static void write8_static(uint32_t address, uint8_t value) {
        if (!instance || address >= instance->memory.size()) return;
        instance->memory[address] = value;
    }
    
    static int pc_hook_static(unsigned int pc) {
        return instance ? instance->OnPcHook(pc) : 0;
    }
};

/* Static member definition */
template<typename Derived>
Derived* M68kMinimalTestBase<Derived>::instance = nullptr;

/* Convenience macro for simple test classes */
#define DECLARE_M68K_TEST(TestName) \
    class TestName : public M68kMinimalTestBase<TestName>

/* Common test assertions */
namespace M68kTestUtils {
    inline void ExpectRegistersCleared() {
        for (int i = M68K_REG_D0; i <= M68K_REG_D7; i++) {
            EXPECT_EQ(m68k_get_reg(NULL, static_cast<m68k_register_t>(i)), 0u) 
                << "Data register D" << (i - M68K_REG_D0) << " should be 0";
        }
        for (int i = M68K_REG_A0; i <= M68K_REG_A6; i++) {
            EXPECT_EQ(m68k_get_reg(NULL, static_cast<m68k_register_t>(i)), 0u)
                << "Address register A" << (i - M68K_REG_A0) << " should be 0";
        }
    }
    
    inline void ExpectFlagsSet(unsigned int sr, unsigned int mask, const char* description) {
        EXPECT_TRUE((sr & mask) == mask) << description << " (SR=" << std::hex << sr << ")";
    }
    
    inline void PrintMemoryDump(const uint8_t* memory, uint32_t start, uint32_t length) {
        printf("Memory dump from 0x%04X:\n", start);
        for (uint32_t i = 0; i < length; i += 16) {
            printf("%04X: ", start + i);
            for (uint32_t j = 0; j < 16 && i + j < length; j++) {
                printf("%02X ", memory[start + i + j]);
            }
            printf("\n");
        }
    }
    // Convenience C++ wrapper returning the disassembly text and size as a pair.
    // This avoids manual buffer management at call sites and makes test
    // expectations concise via std::make_pair.
    inline std::pair<std::string, int> m68k_disassembly(
        uint32_t pc,
        unsigned int cpu_type = M68K_CPU_TYPE_68000) {
        char buf[256];
        unsigned int size = m68k_disassemble(buf, pc, cpu_type);
        return std::make_pair(std::string(buf), static_cast<int>(size));
    }
}

#endif // M68K_TEST_COMMON_H
