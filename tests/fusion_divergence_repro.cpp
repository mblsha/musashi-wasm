#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "m68k.h"
}

extern "C" {
    void reset_myfunc_state();
    void clear_pc_hook_addrs();
    void clear_pc_hook_func();
    void clear_regions();
    void set_read_mem_func(int (*func)(unsigned int, int));
    void set_write_mem_func(void (*func)(unsigned int, int, unsigned int));
    unsigned long long m68k_step_one(void);
    void m68k_pulse_reset(void);
}

namespace {
constexpr uint32_t CALL_ENTRY = 0x00000400u;
constexpr uint32_t MOVEM_PC = CALL_ENTRY;
constexpr uint32_t JSR1_PC = CALL_ENTRY + 4u;
constexpr uint32_t RETURN_PC = CALL_ENTRY + 10u;
constexpr uint32_t TARGET_A = 0x0005DC1Cu;
constexpr uint32_t STACK_BASE = 0x0010F300u;
constexpr uint32_t RAM_BASE = 0x00100000u;
constexpr uint32_t RAM_SIZE = 0x00100000u;
constexpr uint32_t ROM_LENGTH = 0x00300000u;
constexpr uint32_t ROM_BANK = 0x00100000u;

struct WriteEvent {
    uint32_t addr;
    int size;
    uint32_t value;
    uint32_t pc;
    uint32_t pc_raw;
    uint16_t sr;
    std::string sr_flags;
    std::string region;
    uint64_t sequence;
    std::vector<std::pair<uint32_t, uint8_t>> bytes;
};

std::vector<uint8_t> g_rom(ROM_LENGTH, 0);
std::vector<uint8_t> g_ram(RAM_SIZE, 0);
std::vector<WriteEvent> g_pending_writes;
uint64_t g_write_sequence = 0;

uint32_t mask24(uint32_t addr) {
    return addr & 0x00FFFFFFu;
}

std::string format_cc_flags(uint16_t sr) {
    std::string flags;
    if (sr & 0x0010u) flags.push_back('X');
    if (sr & 0x0008u) flags.push_back('N');
    if (sr & 0x0004u) flags.push_back('Z');
    if (sr & 0x0002u) flags.push_back('V');
    if (sr & 0x0001u) flags.push_back('C');
    return flags;
}

std::string classify_address(uint32_t addr) {
    if (addr >= STACK_BASE - 0x100u && addr < STACK_BASE + 0x1000u) return "stack";
    if (addr >= RAM_BASE && addr < RAM_BASE + RAM_SIZE) return "ram";
    if (addr < ROM_BANK) return "rom0";
    if (addr >= 0x00200000u && addr < 0x00200000u + ROM_BANK) return "rom1";
    return "misc";
}

std::vector<std::pair<uint32_t, uint8_t>> bytes_of(int size, uint32_t addr, uint32_t value) {
    std::vector<std::pair<uint32_t, uint8_t>> result;
    result.reserve(static_cast<size_t>(size));
    for (int i = 0; i < size; ++i) {
        const int shift = (size - 1 - i) * 8;
        const uint8_t byte = static_cast<uint8_t>((value >> shift) & 0xFFu);
        result.emplace_back(addr + i, byte);
    }
    return result;
}

uint32_t load_be(const std::vector<uint8_t>& buf, uint32_t offset, int size) {
    uint32_t value = 0;
    for (int i = 0; i < size; ++i) {
        value = (value << 8) | buf[offset + static_cast<uint32_t>(i)];
    }
    return value;
}

void store_be(std::vector<uint8_t>& buf, uint32_t offset, int size, uint32_t value) {
    for (int i = 0; i < size; ++i) {
        const int shift = (size - 1 - i) * 8;
        buf[offset + static_cast<uint32_t>(i)] = static_cast<uint8_t>((value >> shift) & 0xFFu);
    }
}

int read_memory(unsigned int address, int size) {
    const uint32_t addr = mask24(address);
    if (addr >= RAM_BASE && addr < RAM_BASE + RAM_SIZE) {
        const uint32_t offset = addr - RAM_BASE;
        if (offset + static_cast<uint32_t>(size) > g_ram.size()) {
            return 0;
        }
        return static_cast<int>(load_be(g_ram, offset, size));
    }
    if (addr < ROM_BANK) {
        if (addr + static_cast<uint32_t>(size) > g_rom.size()) {
            return 0;
        }
        return static_cast<int>(load_be(g_rom, addr, size));
    }
    if (addr >= 0x00200000u && addr < 0x00200000u + ROM_BANK) {
        const uint32_t index = (addr - 0x00200000u) + ROM_BANK;
        if (index + static_cast<uint32_t>(size) > g_rom.size()) {
            return 0;
        }
        return static_cast<int>(load_be(g_rom, index, size));
    }
    return 0;
}

void write_memory(unsigned int address, int size, unsigned int value) {
    const uint32_t addr = mask24(address);
    const uint32_t pc_raw = static_cast<uint32_t>(m68k_get_reg(nullptr, M68K_REG_PC));
    const uint32_t pc = mask24(pc_raw);
    const uint16_t sr = static_cast<uint16_t>(m68k_get_reg(nullptr, M68K_REG_SR));
    const std::string sr_flags = format_cc_flags(sr);
    const std::string region = classify_address(addr);
    const uint64_t seq = ++g_write_sequence;
    if (addr >= RAM_BASE && addr < RAM_BASE + RAM_SIZE) {
        const uint32_t offset = addr - RAM_BASE;
        if (offset + static_cast<uint32_t>(size) <= g_ram.size()) {
            store_be(g_ram, offset, size, value);
        }
    }
    g_pending_writes.push_back(WriteEvent{addr, size, value, pc, pc_raw, sr, sr_flags, region, seq,
                                          bytes_of(size, addr, value)});
}

struct StepInfo {
    uint32_t start_pc_raw;
    uint32_t end_pc_raw;
    uint32_t start_pc;
    uint32_t end_pc;
    uint16_t start_sr;
    uint16_t end_sr;
    unsigned long long cycles;
    uint32_t ir;
    uint32_t word_at_pc;
    uint32_t word_at_pc_plus_two;
    std::vector<WriteEvent> writes;
};

StepInfo step_cpu() {
    g_pending_writes.clear();
    const uint32_t start_pc_raw = static_cast<uint32_t>(m68k_get_reg(nullptr, M68K_REG_PC));
    const uint32_t start_pc = mask24(start_pc_raw);
    const uint16_t start_sr = static_cast<uint16_t>(m68k_get_reg(nullptr, M68K_REG_SR));
    const uint32_t word0 = static_cast<uint32_t>(read_memory(start_pc, 2)) & 0xFFFFu;
    const uint32_t word1 = static_cast<uint32_t>(read_memory(mask24(start_pc + 2u), 2)) & 0xFFFFu;
    const unsigned long long cycles = m68k_step_one();
    const uint32_t end_pc_raw = static_cast<uint32_t>(m68k_get_reg(nullptr, M68K_REG_PC));
    const uint32_t end_pc = mask24(end_pc_raw);
    const uint16_t end_sr = static_cast<uint16_t>(m68k_get_reg(nullptr, M68K_REG_SR));
    const uint32_t ir = static_cast<uint32_t>(m68k_get_reg(nullptr, M68K_REG_IR)) & 0xFFFFu;
    StepInfo info{start_pc_raw,
                  end_pc_raw,
                  start_pc,
                  end_pc,
                  start_sr,
                  end_sr,
                  cycles,
                  ir,
                  word0,
                  word1,
                  g_pending_writes};
    return info;
}

void write_bytes(std::vector<uint8_t>& buf, uint32_t addr, const std::vector<uint8_t>& bytes) {
    for (size_t i = 0; i < bytes.size(); ++i) {
        buf[addr + static_cast<uint32_t>(i)] = bytes[i];
    }
}

void write_long_be(std::vector<uint8_t>& buf, uint32_t addr, uint32_t value) {
    store_be(buf, addr, 4, value);
}

std::map<uint32_t, uint8_t> to_byte_map(const std::vector<WriteEvent>& writes) {
    std::map<uint32_t, uint8_t> result;
    for (const auto& evt : writes) {
        for (const auto& entry : evt.bytes) {
            result[entry.first] = entry.second;
        }
    }
    return result;
}

void dump_registers() {
    const uint32_t d0 = static_cast<uint32_t>(m68k_get_reg(nullptr, M68K_REG_D0));
    const uint32_t d1 = static_cast<uint32_t>(m68k_get_reg(nullptr, M68K_REG_D1));
    const uint32_t a0 = static_cast<uint32_t>(m68k_get_reg(nullptr, M68K_REG_A0));
    const uint32_t a1 = static_cast<uint32_t>(m68k_get_reg(nullptr, M68K_REG_A1));
    const uint32_t sp = static_cast<uint32_t>(m68k_get_reg(nullptr, M68K_REG_A7));
    const uint16_t sr = static_cast<uint16_t>(m68k_get_reg(nullptr, M68K_REG_SR));
    const uint32_t pc = mask24(static_cast<uint32_t>(m68k_get_reg(nullptr, M68K_REG_PC)));
    std::printf(
        "Local registers: d0=0x%08X d1=0x%08X a0=0x%08X a1=0x%08X sp=0x%08X sr=0x%04X [%s] pc=0x%06X\n",
        d0,
        d1,
        a0,
        a1,
        sp,
        sr,
        format_cc_flags(sr).c_str(),
        pc);
}

void dump_writes(const std::map<uint32_t, uint8_t>& map) {
    if (map.empty()) {
        std::puts("writes=<none>");
        return;
    }
    std::printf("writes=");
    bool first = true;
    for (const auto& [addr, value] : map) {
        if (!first) {
            std::printf(", ");
        }
        first = false;
        std::printf("%06X:%02X", addr, value);
    }
    std::printf("\n");
}

} // namespace

int main() {
    reset_myfunc_state();
    clear_pc_hook_addrs();
    clear_pc_hook_func();
    clear_regions();

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    set_read_mem_func(read_memory);
    set_write_mem_func(write_memory);

    std::fill(g_rom.begin(), g_rom.end(), 0);
    std::fill(g_ram.begin(), g_ram.end(), 0);
    g_write_sequence = 0;

    write_long_be(g_rom, 0x000000u, STACK_BASE);
    write_long_be(g_rom, 0x000004u, CALL_ENTRY);

    for (uint32_t vec = 2; vec < 32; ++vec) {
        const uint32_t sentinel = 0xDEAD0000u | (vec & 0xFFFFu);
        write_long_be(g_rom, vec * 4u, sentinel);
    }

    write_bytes(g_rom, MOVEM_PC, {0x48, 0xE7, 0xFF, 0xFE});
    write_bytes(g_rom, JSR1_PC, {0x4E, 0xB9, 0x00, 0x05, 0xDC, 0x1C});
    write_bytes(g_rom, RETURN_PC, {0x4E, 0x75});

    write_bytes(g_rom, TARGET_A, {0x30, 0x3C, 0x00, 0x9C});
    write_bytes(g_rom, TARGET_A + 4u, {0x21, 0xBC, 0xFF, 0xFF, 0xFF, 0xFF});
    write_bytes(g_rom, TARGET_A + 10u, {0x4E, 0x75});

    m68k_pulse_reset();

    m68k_set_reg(M68K_REG_A7, STACK_BASE);
    m68k_set_reg(M68K_REG_SP, STACK_BASE);
    m68k_set_reg(M68K_REG_A0, 0x00100A80u);
    m68k_set_reg(M68K_REG_A1, 0x00100A80u);
    m68k_set_reg(M68K_REG_D0, 0x0000009Cu);
    m68k_set_reg(M68K_REG_D1, 0);
    m68k_set_reg(M68K_REG_SR, 0x2704u);
    m68k_set_reg(M68K_REG_PC, CALL_ENTRY);

    const unsigned step_limit = 200000;
    for (unsigned step = 0; step < step_limit; ++step) {
        const StepInfo info = step_cpu();
        const bool end_is_zero = (info.end_pc_raw == 0);
        const bool end_is_sentinel = (info.end_pc_raw & 0xFFFF0000u) == 0xDEAD0000u;
        if (end_is_zero || end_is_sentinel) {
            const auto write_map = to_byte_map(info.writes);
            std::printf("--- Native divergence detected ---\n");
            std::printf("Step %u\n", step);
            std::printf(
                "  Local: PC %06X (raw 0x%08X) -> %06X (raw 0x%08X), SR 0x%04X[%s]->0x%04X[%s], cycles=%llu IR=0x%04X word0=0x%04X word1=0x%04X\n",
                info.start_pc,
                info.start_pc_raw,
                info.end_pc,
                info.end_pc_raw,
                info.start_sr,
                format_cc_flags(info.start_sr).c_str(),
                info.end_sr,
                format_cc_flags(info.end_sr).c_str(),
                info.cycles,
                info.ir,
                info.word_at_pc,
                info.word_at_pc_plus_two);
            dump_writes(write_map);
            dump_registers();
            std::map<uint64_t, WriteEvent> ordered;
            for (const auto& evt : info.writes) {
                ordered.emplace(evt.sequence, evt);
            }
            size_t total_bytes = 0;
            for (const auto& evt : info.writes) {
                total_bytes += evt.bytes.size();
            }
            std::printf("  total write bytes this step: %zu\n", total_bytes);
            for (const auto& [seq_key, evt] : ordered) {
                std::printf(
                    "    write[#%llu]: addr=0x%06X (%s) size=%d value=0x%08X pc=0x%06X rawPc=0x%08X sr=0x%04X[%s]\n",
                    static_cast<unsigned long long>(evt.sequence),
                    evt.addr,
                    evt.region.c_str(),
                    evt.size,
                    evt.value,
                    evt.pc,
                    evt.pc_raw,
                    evt.sr,
                    evt.sr_flags.c_str());
            }
            const bool has_first_byte = write_map.count(0x00100A80u) > 0;
            std::printf("  contains 0x00100A80? %s\n", has_first_byte ? "yes" : "no");
            if (end_is_sentinel) {
                const uint32_t vector = info.end_pc_raw & 0xFFFFu;
                std::printf("  exception vector used: %u (0x%04X)\n", vector, vector);
            }
            return EXIT_SUCCESS;
        }
    }

    std::puts("No divergence observed within 200000 steps.");
    return EXIT_SUCCESS;
}
