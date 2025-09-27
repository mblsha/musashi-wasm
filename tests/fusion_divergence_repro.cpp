#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

#include <gtest/gtest.h>

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
}

namespace {

constexpr uint32_t kCallEntry = 0x00000400u;
constexpr uint32_t kCalleeEntry = 0x0005DC1Cu;
constexpr uint32_t kStackBase = 0x0010F300u;
constexpr uint32_t kRamBase = 0x00100000u;
constexpr uint32_t kRamSize = 0x00100000u;
constexpr uint32_t kRomLength = 0x00300000u;
constexpr uint32_t kA0InitialValue = 0x00100A80u;
constexpr uint32_t kA0WriteFirstByte = kA0InitialValue;

std::array<uint8_t, kRomLength> g_rom{};
std::array<uint8_t, kRamSize> g_ram{};

struct WriteEvent {
  uint32_t addr;
  int size;
  uint32_t value;
};

std::vector<WriteEvent> g_writes;

struct TraceWrite {
  uint32_t step;
  WriteEvent event;
  uint32_t pc;
};

struct TraceRead {
  uint32_t step;
  uint32_t addr;
  int size;
  uint32_t value;
  uint32_t pc;
};

std::vector<TraceWrite> g_write_log;
std::vector<TraceRead> g_read_log;
uint32_t g_current_step = 0;

constexpr uint32_t Mask24(uint32_t value) { return value & 0x00FFFFFFu; }

void WriteLongBE(uint32_t addr, uint32_t value) {
  g_rom[addr + 0] = static_cast<uint8_t>((value >> 24) & 0xFFu);
  g_rom[addr + 1] = static_cast<uint8_t>((value >> 16) & 0xFFu);
  g_rom[addr + 2] = static_cast<uint8_t>((value >> 8) & 0xFFu);
  g_rom[addr + 3] = static_cast<uint8_t>(value & 0xFFu);
}

void WriteBytes(uint32_t addr, std::initializer_list<uint8_t> bytes) {
  size_t index = 0;
  for (uint8_t byte : bytes) {
    g_rom[addr + static_cast<uint32_t>(index++)] = byte;
  }
}

int ReadMemory(unsigned int address, int size) {
  const uint32_t addr = Mask24(address);
  const uint32_t pc = Mask24(static_cast<uint32_t>(m68k_get_reg(nullptr, M68K_REG_PC)));
  if ((size > 1) && (addr & 1u)) {
    ADD_FAILURE() << "Unaligned read size=" << size << " addr=0x" << std::hex << addr;
  }

  uint32_t value = 0;
  const uint32_t end = addr + static_cast<uint32_t>(size);

  if (addr >= kRamBase && end <= kRamBase + kRamSize) {
    const uint32_t offset = addr - kRamBase;
    for (int i = 0; i < size; ++i) {
      value = (value << 8) | g_ram[offset + static_cast<uint32_t>(i)];
    }
  } else if (end <= kRomLength) {
    for (int i = 0; i < size; ++i) {
      value = (value << 8) | g_rom[addr + static_cast<uint32_t>(i)];
    }
  } else {
    value = 0;
  }

  g_read_log.push_back({g_current_step, addr, size, value, pc});

  if (size == 1) return static_cast<int>(value & 0xFFu);
  if (size == 2) return static_cast<int>(value & 0xFFFFu);
  return static_cast<int>(value);
}

void WriteMemory(unsigned int address, int size, unsigned int value) {
  const uint32_t addr = Mask24(address);
  const uint32_t pc = Mask24(static_cast<uint32_t>(m68k_get_reg(nullptr, M68K_REG_PC)));
  if ((size > 1) && (addr & 1u)) {
    ADD_FAILURE() << "Unaligned write size=" << size << " addr=0x" << std::hex << addr;
  }
  if (addr >= kRamBase && addr < kRamBase + kRamSize) {
    const uint32_t offset = addr - kRamBase;
    if (size >= 1) g_ram[offset] = static_cast<uint8_t>((value >> ((size - 1) * 8)) & 0xFFu);
    if (size >= 2) g_ram[offset + 1] = static_cast<uint8_t>((value >> ((size - 2) * 8)) & 0xFFu);
    if (size >= 3) g_ram[offset + 2] = static_cast<uint8_t>((value >> ((size - 3) * 8)) & 0xFFu);
    if (size >= 4) g_ram[offset + 3] = static_cast<uint8_t>(value & 0xFFu);
  }
  g_writes.push_back({addr, size, value});
  g_write_log.push_back({g_current_step, {addr, size, value}, pc});
}

void InitializeRom() {
  g_rom.fill(0);
  WriteLongBE(0x000000u, kStackBase);
  WriteLongBE(0x000004u, kCallEntry);
  for (uint32_t vec = 2; vec < 32; ++vec) {
    WriteLongBE(vec * 4u, 0xDEAD0000u | (vec & 0xFFFFu));
  }
  WriteBytes(kCallEntry, {0x48, 0xE7, 0xFF, 0xFE});
  WriteBytes(kCallEntry + 4u, {0x4E, 0xB9, 0x00, 0x05, 0xDC, 0x1C});
  WriteBytes(kCallEntry + 10u, {0x4E, 0x75});
  WriteBytes(kCalleeEntry, {0x30, 0x3C, 0x00, 0x9C});
  // MOVE.L #$11223344,(A0)
  WriteBytes(kCalleeEntry + 4u, {0x20, 0xFC, 0x11, 0x22, 0x33, 0x44});
  WriteBytes(kCalleeEntry + 10u, {0x4E, 0x75});
}

void InitializeCpu() {
  reset_myfunc_state();
  clear_pc_hook_addrs();
  clear_pc_hook_func();
  clear_regions();
  set_read_mem_func(ReadMemory);
  set_write_mem_func(WriteMemory);

  m68k_init();
  m68k_set_cpu_type(M68K_CPU_TYPE_68000);
  g_ram.fill(0);
  m68k_pulse_reset();

  m68k_set_reg(M68K_REG_A7, kStackBase);
  m68k_set_reg(M68K_REG_SP, kStackBase);
  m68k_set_reg(M68K_REG_A0, kA0InitialValue);
  m68k_set_reg(M68K_REG_A1, kA0InitialValue);
  m68k_set_reg(M68K_REG_D0, 0x0000009Cu);
  m68k_set_reg(M68K_REG_D1, 0);
  m68k_set_reg(M68K_REG_SR, 0x2704u);
  m68k_set_reg(M68K_REG_PC, kCallEntry);
}

bool StepUntilExitOrWrite(uint32_t target_addr) {
  constexpr uint32_t kStepLimit = 200000;
  g_write_log.clear();
  g_read_log.clear();
  for (uint32_t step = 0; step < kStepLimit; ++step) {
    g_current_step = step;
    g_writes.clear();
    m68k_step_one();
    for (const auto& write : g_writes) {
      for (int i = 0; i < write.size; ++i) {
        if (write.addr + static_cast<uint32_t>(i) == target_addr) {
          return true;
        }
      }
    }
    const uint32_t pc = Mask24(static_cast<uint32_t>(m68k_get_reg(nullptr, M68K_REG_PC)));
    if (pc == 0 || (pc & 0x00FF0000u) == 0x00AD0000u) {
      break;
    }
  }
  return false;
}

class FusionDivergenceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    InitializeRom();
    InitializeCpu();
  }
};

TEST_F(FusionDivergenceTest, EmitsA0DirectWrite) {
  const bool observed = StepUntilExitOrWrite(kA0WriteFirstByte);
  EXPECT_TRUE(observed)
      << "expected write to 0x" << std::hex << kA0WriteFirstByte << " not observed";
  if (!observed) {
    for (const auto& entry : g_write_log) {
      const auto& write = entry.event;
      std::cerr << "write step=" << std::dec << entry.step
                << " pc=0x" << std::hex << entry.pc
                << " addr=0x" << write.addr
                << " size=" << std::dec << write.size
                << " value=0x" << std::hex << write.value << std::endl;
    }
    for (const auto& read : g_read_log) {
      std::cerr << "read step=" << std::dec << read.step
                << " pc=0x" << std::hex << read.pc
                << " addr=0x" << read.addr
                << " size=" << std::dec << read.size
                << " value=0x" << std::hex << read.value << std::endl;
    }
  } else {
    const uint32_t offset = kA0InitialValue - kRamBase;
    ASSERT_LT(offset + 3u, static_cast<uint32_t>(g_ram.size()));
    EXPECT_EQ(g_ram[offset + 0], 0x11);
    EXPECT_EQ(g_ram[offset + 1], 0x22);
    EXPECT_EQ(g_ram[offset + 2], 0x33);
    EXPECT_EQ(g_ram[offset + 3], 0x44);
  }
}

}  // namespace
