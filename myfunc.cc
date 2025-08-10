#include <stdio.h>
#include "m68k.h"

#include <cstdint>
#include <unordered_set>
#include <optional>
#include <vector>

extern "C" {
typedef int (*read_mem_t)(unsigned int address, int size);
typedef void (*write_mem_t)(unsigned int address, int size, unsigned int value);
typedef int (*pc_hook_t)(unsigned int pc);
} // extern "C"

static bool _initialized = false;
static bool _enable_printf_logging = false;
static read_mem_t _read_mem = 0;
static write_mem_t _write_mem = 0;
static pc_hook_t _pc_hook = 0;
static std::unordered_set<unsigned int> _pc_hook_addrs;

struct Region {
  unsigned int start_;
  unsigned int size_;
  uint8_t* data_;

  Region(unsigned int start, unsigned int size, void* data)
    : start_(start), size_(size), data_(static_cast<uint8_t*>(data))
  {}
  // Note: Region does not own the memory, caller is responsible for cleanup

  std::optional<unsigned int> read(unsigned int addr, int size) {
    if (addr >= start_ && addr < start_ + size_) {
      if (size == 1) {
        return data_[addr - start_];
      } else if (size == 2) {
        return (data_[addr - start_ + 0] << 8) | data_[addr - start_ + 1];
      } else if (size == 4) {
        return (
          (data_[addr - start_ + 0] << 24) |
          (data_[addr - start_ + 1] << 16) |
          (data_[addr - start_ + 2] << 8) |
          data_[addr - start_ + 3]
        );
      }
    }
    return std::nullopt;
  }
};
static std::vector<Region> _regions;

extern "C" {
  int my_initialize() {
    int result = _initialized;
    _initialized = true;
    return result;
  }
  void enable_printf_logging() {
    printf("enable_printf_logging\n");
    _enable_printf_logging = true;
  }
  void set_read_mem_func(read_mem_t func) {
    if (_enable_printf_logging)
      printf("set_read_mem_func: %p\n", (void*)func);
     _read_mem = func;
  }
  void set_write_mem_func(write_mem_t func) {
    if (_enable_printf_logging)
      printf("set_write_mem_func: %p\n", (void*)func);
    _write_mem = func;
  }
  void set_pc_hook_func(pc_hook_t func) {
    if (_enable_printf_logging)
      printf("set_pc_hook_func: %p\n", (void*)func);
    _pc_hook = func;
  }
  void add_pc_hook_addr(unsigned int addr) {
    if (_enable_printf_logging)
      printf("add_pc_hook_addr: %p\n", (void*)addr);
    _pc_hook_addrs.insert(addr);
  }
  void add_region(unsigned int start, unsigned int size, void* data) {
    if (_enable_printf_logging)
      printf("add_region: %p %p %p\n", (void*)start, (void*)size, data);
    _regions.emplace_back(start, size, data);
  }
  void clear_regions() {
    _regions.clear();
  }
} // extern "C"

unsigned int my_read_memory(unsigned int address, int size) {
  for (auto& region : _regions) {
    const auto val = region.read(address, size);
    if (val) {
      return *val;
    }
  }
  if (_read_mem) {
    return _read_mem(address, size);
  }
  return 0; // Return 0 if no handler is set
}

unsigned int m68k_read_memory_8(unsigned int address) { return my_read_memory(address, 1); }
unsigned int m68k_read_memory_16(unsigned int address) { return my_read_memory(address, 2); }
unsigned int m68k_read_memory_32(unsigned int address) { return my_read_memory(address, 4); }

void m68k_write_memory_8(unsigned int address, unsigned int value) { 
  if (_write_mem) _write_mem(address, 1, value); 
}
void m68k_write_memory_16(unsigned int address, unsigned int value) { 
  if (_write_mem) _write_mem(address, 2, value); 
}
void m68k_write_memory_32(unsigned int address, unsigned int value) { 
  if (_write_mem) _write_mem(address, 4, value); 
}

int my_instruction_hook_function(unsigned int pc) {
  // Only call the hook if it's set and we want to break on this address
  if (_pc_hook) {
    // Optionally check if we have specific addresses to hook
    // if (_pc_hook_addrs.find(pc) != _pc_hook_addrs.end()) {
    //   return _pc_hook(pc);
    // }
    return _pc_hook(pc);
  }
  return 0; // Return 0 to continue execution
}
