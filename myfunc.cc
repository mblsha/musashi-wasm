#include <stdio.h>
#include "m68k.h"
#include "m68ktrace.h"
#include "m68k_perfetto.h"

#include <cstdint>
#include <unordered_set>
#include <unordered_map>
#include <optional>
#include <vector>
#include <string>

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
  
  bool write(unsigned int addr, int size, unsigned int value) {
    if (addr >= start_ && addr < start_ + size_) {
      if (size == 1) {
        data_[addr - start_] = value & 0xFF;
      } else if (size == 2) {
        data_[addr - start_ + 0] = (value >> 8) & 0xFF;
        data_[addr - start_ + 1] = value & 0xFF;
      } else if (size == 4) {
        data_[addr - start_ + 0] = (value >> 24) & 0xFF;
        data_[addr - start_ + 1] = (value >> 16) & 0xFF;
        data_[addr - start_ + 2] = (value >> 8) & 0xFF;
        data_[addr - start_ + 3] = value & 0xFF;
      }
      return true;
    }
    return false;
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
  
  /* ======================================================================== */
  /* ==================== SYMBOL NAMING FOR PERFETTO ====================== */
  /* ======================================================================== */
  
  static std::unordered_map<unsigned int, std::string> _function_names;
  static std::unordered_map<unsigned int, std::string> _memory_names;
  
  void register_function_name(unsigned int address, const char* name) {
    if (name) {
      _function_names[address] = name;
      if (_enable_printf_logging)
        printf("register_function_name: 0x%08X = '%s'\n", address, name);
    }
  }
  
  void register_memory_name(unsigned int address, const char* name) {
    if (name) {
      _memory_names[address] = name;
      if (_enable_printf_logging)
        printf("register_memory_name: 0x%08X = '%s'\n", address, name);
    }
  }
  
  void register_memory_range(unsigned int start, unsigned int size, const char* name) {
    if (name) {
      /* Register the base address and optionally key addresses within the range */
      _memory_names[start] = name;
      /* Also register the range name with size suffix for clarity */
      _memory_names[start] = std::string(name) + "[" + std::to_string(size) + "]";
      if (_enable_printf_logging)
        printf("register_memory_range: 0x%08X-0x%08X = '%s'\n", start, start + size - 1, name);
    }
  }
  
  void clear_registered_names() {
    _function_names.clear();
    _memory_names.clear();
    if (_enable_printf_logging)
      printf("clear_registered_names: cleared all names\n");
  }
  
  const char* get_function_name(unsigned int address) {
    auto it = _function_names.find(address);
    return (it != _function_names.end()) ? it->second.c_str() : nullptr;
  }
  
  const char* get_memory_name(unsigned int address) {
    auto it = _memory_names.find(address);
    return (it != _memory_names.end()) ? it->second.c_str() : nullptr;
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

void my_write_memory(unsigned int address, int size, unsigned int value) {
  // Check regions first, similar to read
  for (auto& region : _regions) {
    if (region.write(address, size, value)) {
      return; // Write handled by region
    }
  }
  // Fall back to callback if no region handles it
  if (_write_mem) {
    _write_mem(address, size, value);
  }
}

void m68k_write_memory_8(unsigned int address, unsigned int value) { 
  my_write_memory(address, 1, value);
}
void m68k_write_memory_16(unsigned int address, unsigned int value) { 
  my_write_memory(address, 2, value);
}
void m68k_write_memory_32(unsigned int address, unsigned int value) { 
  my_write_memory(address, 4, value);
}

/* Disassembler memory read functions */
unsigned int m68k_read_disassembler_8(unsigned int address) {
  return m68k_read_memory_8(address);
}

unsigned int m68k_read_disassembler_16(unsigned int address) {
  return m68k_read_memory_16(address);
}

unsigned int m68k_read_disassembler_32(unsigned int address) {
  return m68k_read_memory_32(address);
}

int my_instruction_hook_function(unsigned int pc) {
  // Only call the hook if it's set
  if (_pc_hook) {
    // If specific addresses were added, only hook those
    if (!_pc_hook_addrs.empty()) {
      if (_pc_hook_addrs.find(pc) != _pc_hook_addrs.end()) {
        return _pc_hook(pc);
      }
    } else {
      // If no specific addresses, hook all
      return _pc_hook(pc);
    }
  }
  return 0; // Return 0 to continue execution
}

// This is the new wrapper called by the core
extern "C" int m68k_instruction_hook_wrapper(unsigned int pc, unsigned int ir, unsigned int cycles) {
    // Call the C++ tracing hook first
    int trace_result = m68k_trace_instruction_hook(pc, ir, cycles);
    if (trace_result != 0) {
        return trace_result; // Tracing wants to stop execution
    }

    // Call the original JS-facing hook
    return my_instruction_hook_function(pc);
}

/* ======================================================================== */
/* ======================= PERFETTO TRACE API WRAPPERS =================== */
/* ======================================================================== */

extern "C" {
  /* Perfetto lifecycle management */
  int perfetto_init(const char* process_name) {
    if (_enable_printf_logging)
      printf("perfetto_init: %s\n", process_name ? process_name : "NULL");
    return m68k_perfetto_init(process_name);
  }
  
  void perfetto_destroy() {
    if (_enable_printf_logging)
      printf("perfetto_destroy\n");
    m68k_perfetto_destroy();
  }
  
  /* Feature enable/disable */
  void perfetto_enable_flow(int enable) {
    if (_enable_printf_logging)
      printf("perfetto_enable_flow: %d\n", enable);
    m68k_perfetto_enable_flow(enable);
  }
  
  void perfetto_enable_memory(int enable) {
    if (_enable_printf_logging)
      printf("perfetto_enable_memory: %d\n", enable);
    m68k_perfetto_enable_memory(enable);
  }
  
  void perfetto_enable_instructions(int enable) {
    if (_enable_printf_logging)
      printf("perfetto_enable_instructions: %d\n", enable);
    m68k_perfetto_enable_instructions(enable);
  }
  
  /* Export trace data (critical for WASM) */
  int perfetto_export_trace(uint8_t** data_out, size_t* size_out) {
    if (_enable_printf_logging)
      printf("perfetto_export_trace: %p %p\n", (void*)data_out, (void*)size_out);
    return m68k_perfetto_export_trace(data_out, size_out);
  }
  
  void perfetto_free_trace_data(uint8_t* data) {
    if (_enable_printf_logging)
      printf("perfetto_free_trace_data: %p\n", (void*)data);
    m68k_perfetto_free_trace_data(data);
  }
  
  /* Native-only file save */
  int perfetto_save_trace(const char* filename) {
    if (_enable_printf_logging)
      printf("perfetto_save_trace: %s\n", filename ? filename : "NULL");
    return m68k_perfetto_save_trace(filename);
  }
  
  /* Status */
  int perfetto_is_initialized() {
    int result = m68k_perfetto_is_initialized();
    if (_enable_printf_logging)
      printf("perfetto_is_initialized: %d\n", result);
    return result;
  }
} // extern "C"
