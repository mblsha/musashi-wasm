// m68k_memory_bridge.cc - Bridge all M68k memory access through region-aware system
#include <cstdint>

// Your existing API (already implemented in myfunc.cc)
extern "C" {
    unsigned int my_read_memory(unsigned int address, int size);
    void my_write_memory(unsigned int address, int size, unsigned int value);
}

namespace {

static inline uint32_t addr24(uint32_t a) { return a & 0x00FFFFFFu; }

template <unsigned int Size>
constexpr unsigned int mask_for_size() {
    static_assert(Size == 1 || Size == 2 || Size == 4, "Unsupported access size");
    if constexpr (Size == 1) {
        return 0xFFu;
    } else if constexpr (Size == 2) {
        return 0xFFFFu;
    } else {
        return 0xFFFFFFFFu;
    }
}

template <unsigned int Size>
unsigned int read_memory(unsigned int address) {
    static_assert(Size == 1 || Size == 2 || Size == 4, "Unsupported access size");
    return my_read_memory(addr24(address), Size);
}

template <unsigned int Size>
void write_memory(unsigned int address, unsigned int value) {
    static_assert(Size == 1 || Size == 2 || Size == 4, "Unsupported access size");
    my_write_memory(addr24(address), Size, value & mask_for_size<Size>());
}

}  // namespace

extern "C" {

// ---- Data read/write callbacks ----
unsigned int m68k_read_memory_8(unsigned int address) {
    return read_memory<1>(address);
}

unsigned int m68k_read_memory_16(unsigned int address) {
    return read_memory<2>(address);
}

unsigned int m68k_read_memory_32(unsigned int address) {
    return read_memory<4>(address);
}

void m68k_write_memory_8(unsigned int address, unsigned int value) {
    write_memory<1>(address, value);
}

void m68k_write_memory_16(unsigned int address, unsigned int value) {
    write_memory<2>(address, value);
}

void m68k_write_memory_32(unsigned int address, unsigned int value) {
    write_memory<4>(address, value);
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
