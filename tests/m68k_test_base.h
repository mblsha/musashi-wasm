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
    void set_read8_callback(int32_t fp);
    void set_write8_callback(int32_t fp);
    void set_probe_callback(int32_t fp);
    
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
        
        set_read8_callback(reinterpret_cast<int32_t>(read8_static));
        set_write8_callback(reinterpret_cast<int32_t>(write8_static));
        set_probe_callback(reinterpret_cast<int32_t>((pc_hook_static)));
        
        m68k_init();
        
        write_long(0, 0x1000);  /* Initial SP */
        write_long(4, 0x400);   /* Initial PC */
        
        m68k_pulse_reset();
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
    
    /* Utility to normalize mnemonics by stripping size suffixes */
    static std::string NormalizeMnemonic(const std::string& s) {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        auto dot = result.find('.');
        if (dot != std::string::npos) result.erase(dot); // strip .w/.l/.b
        return result;
    }
    
    /* Trace analysis utilities */
    int CountInstructionType(const std::string& pattern) {
        int count = 0;
        std::string norm_pattern = NormalizeMnemonic(pattern);
        
        for (const auto& t : trace) {
            std::string norm_mnemonic = NormalizeMnemonic(t.mnemonic);
            
            // For branch detection, be more precise
            if (pattern == "b") {
                // Only count actual branch instructions, not .b size suffixes
                if (norm_mnemonic == "bra" || norm_mnemonic == "bsr" || 
                    norm_mnemonic == "bcc" || norm_mnemonic == "bcs" || 
                    norm_mnemonic == "beq" || norm_mnemonic == "bne" || 
                    norm_mnemonic == "bge" || norm_mnemonic == "bgt" || 
                    norm_mnemonic == "ble" || norm_mnemonic == "blt" || 
                    norm_mnemonic == "bhi" || norm_mnemonic == "bls" || 
                    norm_mnemonic == "bmi" || norm_mnemonic == "bpl" || 
                    norm_mnemonic == "bvc" || norm_mnemonic == "bvs" ||
                    norm_mnemonic.substr(0, 2) == "db") { // dbcc variants
                    count++;
                }
            } else {
                // For CMP instructions, match by prefix to catch cmp, cmpi, cmpa, cmpm
                if (norm_pattern == "cmp" && norm_mnemonic.rfind("cmp", 0) == 0) {
                    count++;
                } else if (norm_mnemonic == norm_pattern) {
                    // For other patterns, do exact match after normalization
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
    
    /* Normalized recursion depth that starts counting from 0 */
    int AnalyzeRecursionDepthNormalized() {
        int max_depth = 0;
        int current_depth = 0;
        bool saw_root = false;
        
        for (const auto& t : trace) {
            std::string norm_mnemonic = NormalizeMnemonic(t.mnemonic);
            
            if (norm_mnemonic == "bsr" || norm_mnemonic == "jsr") {
                if (!saw_root) {
                    saw_root = true;      // root call establishes depth 0
                } else {
                    current_depth++;      // deeper levels start after root
                    max_depth = std::max(max_depth, current_depth);
                }
            } else if (norm_mnemonic == "rts") {
                if (saw_root && current_depth > 0) current_depth--;
            }
        }
        return max_depth;
    }
    
    void PrintCallGraph() {
        printf("\n=== FUNCTION CALL GRAPH ===\n");
        int depth = 0;
        
        for (const auto& instr : trace) {
            std::string lower_mnemonic = instr.mnemonic;
            std::transform(lower_mnemonic.begin(), lower_mnemonic.end(), 
                          lower_mnemonic.begin(), ::tolower);
            
            if (lower_mnemonic == "bsr") {
                for (int i = 0; i < depth; i++) printf("  ");
                printf("→ CALL %s (D0=%d, D1=%d, D2=%d)\n", 
                       instr.operands.c_str(), instr.d0, instr.d1, instr.d2);
                depth++;
            } else if (lower_mnemonic == "rts") {
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

    static uint8_t read8_static(uint32_t address) {
        if (!instance || address >= instance->memory.size()) return 0;
        return instance->memory[address];
    }
    static void write8_static(uint32_t address, uint8_t value) {
        if (!instance || address >= instance->memory.size()) return;
        instance->memory[address] = value;
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
