/* Base class for M68K tests with common functionality */

#ifndef M68K_TEST_BASE_H
#define M68K_TEST_BASE_H

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <cctype>
#include "m68k.h"
#include "m68ktrace.h"

extern "C" {
    void set_read_mem_func(int (*func)(unsigned int address, int size));
    void set_write_mem_func(void (*func)(unsigned int address, int size, unsigned int value));
    void set_pc_hook_func(int (*func)(unsigned int pc));
    
    unsigned int m68k_disassemble(char* str_buff, unsigned int pc, unsigned int cpu_type);
    unsigned int m68k_read_disassembler_8(unsigned int address);
    unsigned int m68k_read_disassembler_16(unsigned int address);
    unsigned int m68k_read_disassembler_32(unsigned int address);
}

/* Base class for M68K tests with instruction tracing support */
class M68kTestBase : public ::testing::Test {
public:
    static M68kTestBase* instance;
    
    /* Instruction trace entry */
    struct InstructionTrace {
        uint32_t pc;
        std::string mnemonic;
        std::string operands;
        std::string full_disasm;
        uint16_t d0, d1, d2;  // Key data registers
        uint32_t a0, sp;      // Key address registers
        
        std::string toString() const {
            std::stringstream ss;
            ss << std::hex << std::setfill('0') << std::setw(6) << pc 
               << ": " << std::left << std::setw(8) << mnemonic 
               << " " << std::setw(20) << operands
               << " [D0=" << std::hex << d0 
               << " D1=" << d1 
               << " D2=" << d2 << "]";
            return ss.str();
        }
    };
    
    /* Memory access */
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

protected:
    std::vector<uint8_t> memory;
    std::vector<InstructionTrace> trace;
    bool enable_tracing = false;
    int instruction_count = 0;
    int total_cycles = 0;
    
    void SetUp() override {
        instance = this;
        memory.resize(1024 * 1024, 0);
        trace.clear();
        enable_tracing = false;
        instruction_count = 0;
        total_cycles = 0;
        
        set_read_mem_func(read_memory_static);
        set_write_mem_func(write_memory_static);
        set_pc_hook_func(pc_hook_static);
        
        m68k_init();
        
        write_long(0, 0x1000);  /* Initial SP */
        write_long(4, 0x400);   /* Initial PC */
        
        m68k_pulse_reset();
        m68k_execute(1);  /* Dummy execution */
    }
    
    void TearDown() override {
        instance = nullptr;
    }
    
    /* Load binary file into memory */
    bool LoadBinaryFile(const std::string& filename, uint32_t load_address) {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file) return false;
        
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        if (load_address + size > memory.size()) return false;
        
        file.read(reinterpret_cast<char*>(&memory[load_address]), size);
        return file.good();
    }
    
    /* Memory utilities */
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
    
    void write_word(uint32_t addr, uint16_t value) {
        memory[addr] = (value >> 8) & 0xFF;
        memory[addr + 1] = value & 0xFF;
    }
    
    /* Trace analysis utilities */
    int CountInstructionType(const std::string& pattern) {
        int count = 0;
        for (const auto& t : trace) {
            // For branch detection, be more precise
            if (pattern == "b") {
                // Only count actual branch instructions, not .b size suffixes
                std::string lower_mnemonic = t.mnemonic;
                std::transform(lower_mnemonic.begin(), lower_mnemonic.end(), 
                              lower_mnemonic.begin(), ::tolower);
                if (lower_mnemonic == "bra" || lower_mnemonic == "bsr" || 
                    lower_mnemonic == "bcc" || lower_mnemonic == "bcs" || 
                    lower_mnemonic == "beq" || lower_mnemonic == "bne" || 
                    lower_mnemonic == "bge" || lower_mnemonic == "bgt" || 
                    lower_mnemonic == "ble" || lower_mnemonic == "blt" || 
                    lower_mnemonic == "bhi" || lower_mnemonic == "bls" || 
                    lower_mnemonic == "bmi" || lower_mnemonic == "bpl" || 
                    lower_mnemonic == "bvc" || lower_mnemonic == "bvs" ||
                    lower_mnemonic.substr(0, 2) == "db") { // dbcc variants
                    count++;
                }
            } else {
                // For other patterns, do case-insensitive exact match
                std::string lower_mnemonic = t.mnemonic;
                std::string lower_pattern = pattern;
                std::transform(lower_mnemonic.begin(), lower_mnemonic.end(), 
                              lower_mnemonic.begin(), ::tolower);
                std::transform(lower_pattern.begin(), lower_pattern.end(), 
                              lower_pattern.begin(), ::tolower);
                if (lower_mnemonic == lower_pattern) {
                    count++;
                }
            }
        }
        return count;
    }
    
    int AnalyzeRecursionDepth() {
        int max_depth = 0;
        int current_depth = 0;
        
        for (const auto& t : trace) {
            // Case-insensitive check for BSR/JSR/RTS
            std::string lower_mnemonic = t.mnemonic;
            std::transform(lower_mnemonic.begin(), lower_mnemonic.end(), 
                          lower_mnemonic.begin(), ::tolower);
            
            if (lower_mnemonic == "bsr" || lower_mnemonic == "jsr") {
                current_depth++;
                max_depth = std::max(max_depth, current_depth);
            } else if (lower_mnemonic == "rts") {
                current_depth--;
            }
        }
        return max_depth;
    }
    
    void PrintCallGraph() {
        printf("\n=== FUNCTION CALL GRAPH ===\n");
        int depth = 0;
        
        for (const auto& instr : trace) {
            if (instr.mnemonic == "bsr") {
                for (int i = 0; i < depth; i++) printf("  ");
                printf("→ CALL %s (D0=%d, D1=%d, D2=%d)\n", 
                       instr.operands.c_str(), instr.d0, instr.d1, instr.d2);
                depth++;
            } else if (instr.mnemonic == "rts") {
                depth--;
                for (int i = 0; i < depth; i++) printf("  ");
                printf("← RETURN\n");
            }
        }
    }
    
    void PrintTrace(int start = -1, int count = -1) {
        if (start < 0) start = 0;
        if (count < 0) count = trace.size();
        
        int end = std::min(start + count, (int)trace.size());
        
        printf("\n=== Instruction Trace ===\n");
        for (int i = start; i < end; i++) {
            const auto& t = trace[i];
            printf("%04d: %s\n", i, t.toString().c_str());
        }
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
        if (enable_tracing && instruction_count < 10000) {  // Limit trace size
            InstructionTrace instr;
            instr.pc = pc;
            
            /* Disassemble instruction */
            char disasm_buf[256];
            m68k_disassemble(disasm_buf, pc, M68K_CPU_TYPE_68000);
            instr.full_disasm = disasm_buf;
            
            /* Parse disassembly into mnemonic and operands */
            std::string full_disasm = disasm_buf;
            size_t space_pos = full_disasm.find_first_of(" \t");
            
            if (space_pos != std::string::npos) {
                instr.mnemonic = full_disasm.substr(0, space_pos);
                
                size_t operand_start = full_disasm.find_first_not_of(" \t", space_pos);
                if (operand_start != std::string::npos) {
                    instr.operands = full_disasm.substr(operand_start);
                }
            } else {
                instr.mnemonic = full_disasm;
            }
            
            /* Capture key registers */
            instr.d0 = m68k_get_reg(NULL, M68K_REG_D0) & 0xFFFF;
            instr.d1 = m68k_get_reg(NULL, M68K_REG_D1) & 0xFFFF;
            instr.d2 = m68k_get_reg(NULL, M68K_REG_D2) & 0xFFFF;
            instr.a0 = m68k_get_reg(NULL, M68K_REG_A0);
            instr.sp = m68k_get_reg(NULL, M68K_REG_SP);
            
            trace.push_back(instr);
            instruction_count++;
        }
        return 0;
    }
};

M68kTestBase* M68kTestBase::instance = nullptr;


#endif // M68K_TEST_BASE_H