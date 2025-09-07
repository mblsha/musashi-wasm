// m68k_memory_bridge.cc - Bridge all M68k memory access through region-aware system
#include <cstdint>

// Your existing API (already implemented in myfunc.cc)
extern "C" {
    unsigned int my_read_memory(unsigned int address, int size);
    void my_write_memory(unsigned int address, int size, unsigned int value);
}

static inline uint32_t addr24(uint32_t a) { return a & 0x00FFFFFFu; }

// Build big-endian words/longs from 8-bit reads to avoid host-endian issues.
// Minimal fix: delegate multi-byte BE reads to my_read_memory to avoid
// double big-endian composition and keep behavior consistent with JS handlers.
static inline unsigned int be16_read(uint32_t a) {
    return my_read_memory(addr24(a), 2);
}

static inline unsigned int be32_read(uint32_t a) {
    return my_read_memory(addr24(a), 4);
}

extern "C" {

// ---- Data read/write callbacks ----
unsigned int m68k_read_memory_8(unsigned int address) { 
    return my_read_memory(addr24(address), 1); 
}

unsigned int m68k_read_memory_16(unsigned int address) { 
    return my_read_memory(addr24(address), 2); 
}

unsigned int m68k_read_memory_32(unsigned int address) { 
    return my_read_memory(addr24(address), 4); 
}

void m68k_write_memory_8(unsigned int address, unsigned int value) { 
    my_write_memory(addr24(address), 1, value & 0xFFu); 
}

void m68k_write_memory_16(unsigned int address, unsigned int value) { 
    my_write_memory(addr24(address), 2, value & 0xFFFFu); 
}

void m68k_write_memory_32(unsigned int address, unsigned int value) { 
    my_write_memory(addr24(address), 4, value); 
}

// Predecrement write for move.l with -(An) destination
void m68k_write_memory_32_pd(unsigned int address, unsigned int value) {
    // For proper 68k behavior, write high word first, then low word
    m68k_write_memory_16(address + 2, (value >> 16) & 0xFFFF);
    m68k_write_memory_16(address, value & 0xFFFF);
}

// ---- Instruction/immediate fetch + PC-relative + disassembler ----
// Route *everything* through the same region-aware path.
unsigned int m68k_read_immediate_8(unsigned int address) { 
    return m68k_read_memory_8(address); 
}

unsigned int m68k_read_immediate_16(unsigned int address) { 
    return m68k_read_memory_16(address); 
}

unsigned int m68k_read_immediate_32(unsigned int address) { 
    return m68k_read_memory_32(address); 
}

unsigned int m68k_read_pcrelative_8(unsigned int address) { 
    return m68k_read_immediate_8(address); 
}

unsigned int m68k_read_pcrelative_16(unsigned int address) { 
    return m68k_read_immediate_16(address); 
}

unsigned int m68k_read_pcrelative_32(unsigned int address) { 
    return m68k_read_immediate_32(address); 
}

unsigned int m68k_read_disassembler_8(unsigned int address) { 
    return m68k_read_immediate_8(address); 
}

unsigned int m68k_read_disassembler_16(unsigned int address) { 
    return m68k_read_immediate_16(address); 
}

unsigned int m68k_read_disassembler_32(unsigned int address) { 
    return m68k_read_immediate_32(address); 
}

} // extern "C"
