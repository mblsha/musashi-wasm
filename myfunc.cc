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
typedef int (*instr_hook_t)(unsigned int pc, unsigned int ir, unsigned int cycles);
} // extern "C"

static bool _initialized = false;
static bool _enable_printf_logging = false;
static read_mem_t _read_mem = nullptr;
static write_mem_t _write_mem = nullptr;
static pc_hook_t _pc_hook = nullptr;
static instr_hook_t _instr_hook = nullptr;  // Full instruction hook (3 params)
static std::unordered_set<unsigned int> _pc_hook_addrs;

// Sentinel return session for JS-driven call() implemented in C++
struct ExecSession {
  bool active = false;
  bool done = false;
  unsigned int sentinel_pc = kAddressSpaceMax - 1; // Full 32-bit even-aligned
};
static ExecSession _exec_session;

// Address space maximum as a static constant. Prefer full 32-bit for sentinel,
// while still accepting 24-bit-masked PCs from a 68000 core.
static constexpr unsigned int kAddressSpaceMax = 0xFFFFFFFFu;

// Helper to detect if current PC equals the active session's sentinel.
static inline bool is_sentinel_pc(unsigned int pc) {
  if (!_exec_session.active) return false;
  const unsigned int sentinel = _exec_session.sentinel_pc;
  return (pc == sentinel) || (norm_pc(pc) == (sentinel & 0x00FFFFFEu));
}

// RAII guard to manage ExecSession lifetime cleanly.
class SessionGuard {
 public:
  explicit SessionGuard(unsigned int entry_pc) {
    _exec_session.active = true;
    _exec_session.done = false;
    _exec_session.sentinel_pc = kAddressSpaceMax - 1; // even-aligned by const
    // Set PC to entry (higher level owns SR/stack)
    m68k_set_reg(M68K_REG_PC, entry_pc);
  }
  ~SessionGuard() {
    _exec_session.active = false;
  }
};

/* ======================================================================== */
/* JavaScript Callback System                                             */
/* ======================================================================== */

// JavaScript callback function pointers
typedef uint8_t (*read8_callback_t)(uint32_t addr);
typedef void (*write8_callback_t)(uint32_t addr, uint8_t val);
typedef int (*probe_callback_t)(uint32_t addr);

static read8_callback_t js_read8_callback = nullptr;
static write8_callback_t js_write8_callback = nullptr;
static probe_callback_t js_probe_callback = nullptr;

// 24-bit address masking for 68000 (16MB address space)
static inline uint32_t addr24(uint32_t addr) {
    return addr & 0x00FFFFFFu;
}

// Big-endian composition functions with address masking
static uint16_t read16_be(uint32_t addr) {
    if (!js_read8_callback) return 0;
    addr = addr24(addr);
    return (js_read8_callback(addr) << 8) | js_read8_callback(addr24(addr + 1));
}

static uint32_t read32_be(uint32_t addr) {
    return ((uint32_t)read16_be(addr) << 16) | read16_be(addr + 2);
}

static void write16_be(uint32_t addr, uint16_t val) {
    if (!js_write8_callback) return;
    addr = addr24(addr);
    js_write8_callback(addr, (val >> 8) & 0xFF);
    js_write8_callback(addr24(addr + 1), val & 0xFF);
}

static void write32_be(uint32_t addr, uint32_t val) {
    write16_be(addr, (val >> 16) & 0xFFFF);
    write16_be(addr + 2, val & 0xFFFF);
}

struct Region {
  unsigned int start_;
  unsigned int size_;
  uint8_t* data_;

  Region(unsigned int start, unsigned int size, void* data)
    : start_(start), size_(size), data_(static_cast<uint8_t*>(data))
  {}
  // Note: Region does not own the memory, caller is responsible for cleanup

  std::optional<unsigned int> read(unsigned int addr, int size) {
    if (addr < start_ || addr + size > start_ + size_) {
      return std::nullopt;
    }
    unsigned int offset = addr - start_;
    unsigned int value = 0;
    for (int i = 0; i < size; ++i) {
      value = (value << 8) | data_[offset + i];
    }
    return value;
  }

  bool write(unsigned int addr, int size, unsigned int value) {
    if (addr < start_ || addr + size > start_ + size_) {
      return false;
    }
    unsigned int offset = addr - start_;
    for (int i = 0; i < size; ++i) {
      data_[offset + i] = (value >> ((size - 1 - i) * 8)) & 0xFF;
    }
    return true;
  }
};
static std::vector<Region> _regions;
static std::unordered_map<unsigned int, std::string> _function_names;
static std::unordered_map<unsigned int, std::string> _memory_names;

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
    _pc_hook = func;
  }
  
  // Full instruction hook setter (3 params: pc, ir, cycles)
  void set_full_instr_hook_func(instr_hook_t func) {
    _instr_hook = func;
  }
  
  // JavaScript callback setters - these are what TypeScript actually calls
  void set_read8_callback(int32_t fp) {
    js_read8_callback = (read8_callback_t)fp;
    if (_enable_printf_logging)
      printf("set_read8_callback: %p\n", (void*)fp);
  }
  
  void set_write8_callback(int32_t fp) {
    js_write8_callback = (write8_callback_t)fp;
    if (_enable_printf_logging)
      printf("set_write8_callback: %p\n", (void*)fp);
  }
  
  void set_probe_callback(int32_t fp) {
    js_probe_callback = (probe_callback_t)fp;
    if (_enable_printf_logging)
      printf("set_probe_callback: %p\n", (void*)fp);
  }
  // Normalize to 24-bit, even address (68k opcodes are word-aligned)
  static inline uint32_t norm_pc(uint32_t a) {
    return (a & 0x00FFFFFEu);
  }
  
  void add_pc_hook_addr(unsigned int addr) {
    if (_enable_printf_logging)
      printf("add_pc_hook_addr: %p (normalized: %p)\n", (void*)addr, (void*)norm_pc(addr));
    _pc_hook_addrs.insert(norm_pc(addr));
  }
  void add_region(unsigned int start, unsigned int size, void* data) {
    if (_enable_printf_logging) {
      printf("DEBUG: add_region called: start=0x%x size=0x%x data=%p (regions before: %zu)\n", 
             start, size, data, _regions.size());
    }
    _regions.emplace_back(start, size, data);
    
    // Debug: verify the region was added properly
    if (_enable_printf_logging) {
      const auto& r = _regions.back();
      printf("DEBUG: Region added successfully: start_=0x%x size_=0x%x data_=%p (total regions: %zu)\n", 
             r.start_, r.size_, (void*)r.data_, _regions.size());
    }
  }
  void clear_regions() {
    _regions.clear();
  }
  void clear_pc_hook_addrs() {
    _pc_hook_addrs.clear();
  }
  
  void clear_pc_hook_func() {
    _pc_hook = nullptr;
  }

  void clear_instr_hook_func() {
    _instr_hook = nullptr;
  }

  // Provide a clean entry helper that sets sane CPU state and jumps to pc
  // without re-priming reset vectors. This avoids spurious early vectoring
  // into BIOS space on hosts without a BIOS mapping.
  void set_entry_point(uint32_t pc) {
    // Supervisor mode, IRQ masked, trace off
    m68k_set_reg(M68K_REG_SR, 0x2700);
    // Clear any pending IRQ
    m68k_set_irq(0);
    // Ensure VBR is 0 on 68000
    m68k_set_reg(M68K_REG_VBR, 0);
    // Set PC last to flush prefetch and take effect immediately
    m68k_set_reg(M68K_REG_PC, pc);
  }
  
  void reset_myfunc_state() {
    _initialized = false;
    _enable_printf_logging = false;
    _read_mem = nullptr;
    _write_mem = nullptr;
    _pc_hook = nullptr;
    _instr_hook = nullptr;
    _pc_hook_addrs.clear();
    _regions.clear();
    _function_names.clear();
    _memory_names.clear();
    _exec_session = ExecSession{};
  }
  
  /* ======================================================================== */
  /* ==================== SYMBOL NAMING FOR PERFETTO ====================== */
  /* ======================================================================== */
  
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

  // Run until JS-side PC hook requests a stop; when that happens,
  // vector PC to sentinel (max address, even-aligned) and return cycles.
  // timeslice is the cycle budget per m68k_execute() burst.
  unsigned long long m68k_call_until_js_stop(unsigned int entry_pc, unsigned int timeslice) {
    if (timeslice == 0) timeslice = 1'000'000u;
    SessionGuard guard(entry_pc);
    unsigned long long total_cycles = 0;
    while (!_exec_session.done) {
      total_cycles += m68k_execute(timeslice);
    }
    return total_cycles;
  }
  
  /* ======================================================================== */
  /* ==================== REGISTER ACCESS HELPERS ========================== */
  /* ======================================================================== */
  
  // Direct register access helpers for TypeScript (avoids enum drift issues)
  void set_d_reg(int n, uint32_t value) {
    if (n >= 0 && n < 8) {
      m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0 + n), value);
    }
  }
  
  uint32_t get_d_reg(int n) {
    if (n >= 0 && n < 8) {
      return m68k_get_reg(NULL, static_cast<m68k_register_t>(M68K_REG_D0 + n));
    }
    return 0;
  }
  
  void set_a_reg(int n, uint32_t value) {
    if (n >= 0 && n < 8) {
      m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_A0 + n), value);
    }
  }
  
  uint32_t get_a_reg(int n) {
    if (n >= 0 && n < 8) {
      return m68k_get_reg(NULL, static_cast<m68k_register_t>(M68K_REG_A0 + n));
    }
    return 0;
  }
  
  void set_pc_reg(uint32_t value) {
    m68k_set_reg(M68K_REG_PC, value);
  }
  
  uint32_t get_pc_reg(void) {
    return m68k_get_reg(NULL, M68K_REG_PC);
  }
  
  void set_sr_reg(uint16_t value) {
    m68k_set_reg(M68K_REG_SR, value);
  }
  
  uint32_t get_sr_reg(void) {
    return m68k_get_reg(NULL, M68K_REG_SR);
  }
  
  void set_isp_reg(uint32_t value) {
    m68k_set_reg(M68K_REG_ISP, value);
  }
  
  void set_usp_reg(uint32_t value) {
    m68k_set_reg(M68K_REG_USP, value);
  }
  
  uint32_t get_sp_reg(void) {
    return m68k_get_reg(NULL, M68K_REG_SP);
  }
} // extern "C"

extern "C" unsigned int my_read_memory(unsigned int address, int size) {
  // Check regions first
  for (auto& region : _regions) {
    const auto val = region.read(address, size);
    if (val) {
      if (_enable_printf_logging && address < 0x100) {
        printf("DEBUG: my_read_memory region hit: addr=0x%x size=%d value=0x%x (region start=0x%x)\n", 
               address, size, *val, region.start_);
      }
      return *val;
    }
  }
  
  // Try JS callback (big-endian composition)
  if (js_read8_callback) {
    unsigned int result;
    switch(size) {
      case 1: result = js_read8_callback(addr24(address)); break;
      case 2: result = read16_be(address); break;
      case 4: result = read32_be(address); break;
      default: result = 0; break;
    }
    if (_enable_printf_logging && address < 0x100) {
      printf("DEBUG: my_read_memory JS callback: addr=0x%x size=%d value=0x%x\n", 
             address, size, result);
    }
    return result;
  }
  
  // Fall back to old callback system
  if (_read_mem) {
    unsigned int result = _read_mem(address, size);
    if (_enable_printf_logging && address < 0x100) {
      printf("DEBUG: my_read_memory old callback: addr=0x%x size=%d value=0x%x callback=%p\n", 
             address, size, result, (void*)_read_mem);
    }
    return result;
  }
  
  if (_enable_printf_logging && address < 0x100) {
    printf("DEBUG: my_read_memory NO HANDLER: addr=0x%x size=%d, %zu regions, callback=%p\n", 
           address, size, _regions.size(), (void*)_read_mem);
  }
  return 0; // Return 0 if no handler is set
}

// Memory access callbacks are now in m68k_memory_bridge.cc

extern "C" void my_write_memory(unsigned int address, int size, unsigned int value) {
  // Check regions first
  for (auto& region : _regions) {
    if (region.write(address, size, value)) {
      return; // Write handled by region
    }
  }
  
  // Try JS callback (big-endian decomposition)
  if (js_write8_callback) {
    switch(size) {
      case 1: js_write8_callback(addr24(address), value & 0xFF); break;
      case 2: write16_be(address, value & 0xFFFF); break;
      case 4: write32_be(address, value); break;
    }
    return;
  }
  
  // Fall back to old callback system
  if (_write_mem) {
    _write_mem(address, size, value);
  }
}

// C-linkage glue functions for musashi_glue.c
extern "C" uint32_t my_read_memory_glue(uint32_t address, int size) {
  return my_read_memory(address, size);
}

extern "C" void my_write_memory_glue(uint32_t address, uint32_t value, int size) {
  my_write_memory(address, size, value);
}

// Forward declaration
int _pc_hook_filtering_aware(unsigned int pc);

int my_instruction_hook_function(unsigned int pc_raw) {
  const uint32_t pc = norm_pc(pc_raw);
  
  if (_enable_printf_logging) {
    static int hook_count = 0;
    if (hook_count < 5) {
      printf("DEBUG: my_instruction_hook_function called with pc=0x%x, _pc_hook=%p, js_probe_callback=%p\n", 
             pc, (void*)_pc_hook, (void*)js_probe_callback);
      hook_count++;
    }
  }
  
  // Call JS probe callback if registered, honoring address filter semantics
  if (js_probe_callback) {
    // When no filter is configured, probe all PCs; otherwise, only probe listed PCs
    bool should_probe = _pc_hook_addrs.empty() || (_pc_hook_addrs.find(pc) != _pc_hook_addrs.end());
    if (should_probe) {
      int js_result = js_probe_callback(pc);
      if (js_result != 0) return js_result;  // JS wants to break
    }
  }
  
  // Call existing PC hook system
  if (_pc_hook) {
    // ALWAYS call the user callback - let it decide whether to ignore the PC
    // This keeps the behavior identical to "no filtering" case
    return _pc_hook_filtering_aware(pc);
  }
  
  return 0;
}

// New filtering-aware callback that handles filtering internally
int _pc_hook_filtering_aware(unsigned int pc) {
  // When _pc_hook_addrs is empty, hook all addresses (backward compatible)
  // When _pc_hook_addrs has entries, only hook those specific addresses
  if (_pc_hook_addrs.empty()) {
    // Hook all addresses - this is the default behavior
    if (_enable_printf_logging) {
      static int hook_all_count = 0;
      if (hook_all_count < 5) {  // Limit debug output
        printf("DEBUG: Hook all addresses mode - calling hook for PC=0x%x\n", pc);
        hook_all_count++;
      }
    }
    return _pc_hook(pc);
  } else {
    // Only hook specific addresses
    static bool debug_once = true;
    if (debug_once && _enable_printf_logging) {
      debug_once = false;
      printf("DEBUG: Filtering mode - looking for addresses:");
      for (auto addr : _pc_hook_addrs) {
        printf(" 0x%x", addr);
      }
      printf("\n");
    }
    
    if (_pc_hook_addrs.find(pc) != _pc_hook_addrs.end()) {
      if (_enable_printf_logging) {
        printf("DEBUG: Address filter match - calling hook for PC=0x%x\n", pc);
      }
      return _pc_hook(pc);
    }
    return 0; // filtered out
  }
}

// This is the new wrapper called by the core
/* Clang/GCC attributes to keep the wrapper from being "too smart". */
#if defined(__clang__) || defined(__GNUC__)
__attribute__((noinline, used))
#endif
extern "C" int m68k_instruction_hook_wrapper(unsigned int pc, unsigned int ir, unsigned int cycles) {
    if (_enable_printf_logging) {
        static int wrapper_count = 0;
        if (wrapper_count < 5) {
            printf("DEBUG: m68k_instruction_hook_wrapper called with pc=0x%x, ir=0x%x, cycles=%d\n", pc, ir, cycles);
            wrapper_count++;
        }
    }
    
#ifdef BUILD_TESTS
    // Trace first (non-breaking)
    (void)m68k_trace_instruction_hook(pc, (uint16_t)ir, (int)cycles);

    // Call full instruction hook if set (gets all 3 params)
    if (_instr_hook) {
        int result = _instr_hook(pc, ir, cycles);
        if (result != 0) return result;
    }

    // Always call the unified hook (JS probe + optional legacy filter)
    // This ensures JS probe callbacks work in tests
    (void)my_instruction_hook_function(pc);  // ignore result in tests
    // Also honor sentinel in tests to let C++ sessions terminate deterministically
    if (is_sentinel_pc(pc)) {
        _exec_session.done = true;
        return 1;
    }
    return 0; // never break in tests unless sentinel
#else
    int trace_result = m68k_trace_instruction_hook(pc, (uint16_t)ir, (int)cycles);
    if (trace_result != 0) { 
        m68k_end_timeslice(); 
        return trace_result; 
    }

    // Call full instruction hook if set (gets all 3 params)
    if (_instr_hook) {
        int result = _instr_hook(pc, ir, cycles);
        if (result != 0) {
            m68k_end_timeslice();
            return result;
        }
    }

    const int js_result = my_instruction_hook_function(pc);
    if (js_result != 0) { 
        // JS requested a stop; vector to sentinel for deterministic exit
        if (_exec_session.active) {
            m68k_set_reg(M68K_REG_PC, _exec_session.sentinel_pc);
            _exec_session.done = true;
        }
        m68k_end_timeslice(); 
        return js_result; 
    }
    // If session is active and we have reached sentinel (accept 24-bit masked), finish
    if (is_sentinel_pc(pc)) {
        _exec_session.done = true;
        m68k_end_timeslice();
        return 1;
    }
    return 0;
#endif
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
