#include <stdio.h>
#include "m68k.h"
#include "m68ktrace.h"
#include "m68k_perfetto.h"
#include "musashi_fault.h"

#include <cstdint>
#include <unordered_set>
#include <unordered_map>
#include <optional>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>

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

// Global constants used throughout
static constexpr unsigned int kAddressSpaceMax = 0xFFFFFFFFu;
static constexpr unsigned int kAddr24Mask      = 0x00FFFFFFu;
static constexpr unsigned int kEvenMask        = ~1u;
static constexpr unsigned int kDefaultTimeslice = 1'000'000u;
static_assert(((kAddressSpaceMax - 1) & 1u) == 0u, "Sentinel must be even-aligned");

// Address policy encapsulating sentinel matching rules (32-bit with 24-bit accept)
// Forward declare hook used later
int my_instruction_hook_function(unsigned int pc);
struct AddrPolicy32 {
  static inline bool matches(unsigned int pc, unsigned int sentinel) {
    const unsigned int mask = (kAddr24Mask & kEvenMask);
    return (pc == sentinel) || ((pc & mask) == (sentinel & mask));
  }
};

// Sentinel return session for JS-driven call() implemented in C++
class SentinelSession {
 public:
  void start(unsigned int entry_pc) {
    active = true;
    done = false;
    sentinel_pc = kAddressSpaceMax - 1; // Full 32-bit even-aligned
    install_sentinel();
    m68k_set_reg(M68K_REG_PC, entry_pc);
  }
  void finish() { active = false; }
  bool isSentinelPc(unsigned int pc) const {
    return active && AddrPolicy32::matches(pc, sentinel_pc);
  }
  void markConsumed() { sentinel_consumed = true; }
  void finalize() {
    if (!sentinel_installed) return;

    if (saved_value_valid) {
      // Restore original 32-bit value at the sentinel slot regardless of break reason.
      m68k_write_memory_32(saved_sp, saved_value);
    }

    if (sentinel_consumed) {
      // RTS consumed the sentinel; undo the extra +4 growth so SP reflects caller view.
      unsigned int sp_now = m68k_get_reg(nullptr, M68K_REG_SP);
      if (sp_now >= 4) {
        m68k_set_reg(M68K_REG_SP, sp_now - 4);
      }
      if (_enable_printf_logging) {
        printf("finalize_sentinel: consumed sp_now=0x%08X restored=0x%08X\n",
               sp_now, sp_now >= 4 ? sp_now - 4 : sp_now);
      }
    } else if (_enable_printf_logging) {
      printf("finalize_sentinel: not consumed, sp=0x%08X\n",
             m68k_get_reg(nullptr, M68K_REG_SP));
    }
  }

  bool active = false;
  bool done = false;
  unsigned int sentinel_pc = kAddressSpaceMax - 1;
  bool sentinel_installed = false;
  bool sentinel_consumed = false;
  unsigned int saved_sp = 0;
  unsigned int saved_value = 0;
  bool saved_value_valid = false;

 private:
  static inline unsigned int mask24(unsigned int value) {
    return value & kAddr24Mask;
  }

  void install_sentinel() {
    // Capture current SP so we can restore it accurately when the session ends.
    saved_sp = mask24(m68k_get_reg(nullptr, M68K_REG_SP));
    sentinel_consumed = false;
    sentinel_installed = false;
    saved_value_valid = true;
    saved_value = m68k_read_memory_32(saved_sp);
    m68k_write_memory_32(saved_sp, sentinel_pc);
    sentinel_installed = true;
    if (_enable_printf_logging) {
      printf("install_sentinel: sp=0x%08X saved=0x%08X sentinel=0x%08X\n",
             saved_sp, saved_value, sentinel_pc);
    }
  }
};
static SentinelSession _exec_session;

// Helper to detect if current PC equals the active session's sentinel.
// (removed free is_sentinel_pc; use _exec_session.isSentinelPc)

// RAII guard to manage ExecSession lifetime cleanly.
class SessionGuard {
 public:
  explicit SessionGuard(unsigned int entry_pc) { _exec_session.start(entry_pc); }
  ~SessionGuard() { _exec_session.finish(); }
};

enum class HookResult : int { Continue = 0, Break = 1 };
enum class BreakReason : int { None = 0, Trace = 1, InstrHook = 2, JsHook = 3, Sentinel = 4, Step = 5 };
static BreakReason _last_break_reason = BreakReason::None;

static inline HookResult finalize_break_request(BreakReason reason, bool allow_break) {
  _last_break_reason = reason;
  if (allow_break) {
    m68k_end_timeslice();
    return HookResult::Break;
  }
  return HookResult::Continue;
}

// Single-step control state
enum class StepState : int { Idle = 0, Arm = 1, BreakNext = 2 };
static StepState _step_state = StepState::Idle;

struct HookContext {
  unsigned int pc;
  unsigned int ir;
  unsigned int cycles;
};

static inline HookResult processHooks(const HookContext& ctx, bool allow_break) {
  // Step handling comes first: allow exactly one instruction, then break
  if (_step_state == StepState::BreakNext) {
    // We are at the start of the next instruction; stop now
    _step_state = StepState::Idle;
    _last_break_reason = BreakReason::Step;
    // Important: do NOT call m68k_end_timeslice() here. That API rewrites
    // m68ki_initial_cycles to the current remaining cycle count and zeros
    // the remaining pool, which causes m68k_execute() to return the leftover
    // timeslice rather than the cycles actually consumed by the previous
    // instruction. For single-step semantics, we want m68k_execute() to
    // return the exact cycles used by the stepped instruction, so we simply
    // request a break and let the execute loop exit naturally.
    return HookResult::Break;
  }
  if (_step_state == StepState::Arm) {
    // Arm break for the next instruction and continue without allowing
    // any other hook to break this instruction.
    _step_state = StepState::BreakNext;
    return HookResult::Continue;
  }

  // Trace first
  int trace_result = m68k_trace_instruction_hook(ctx.pc, (uint16_t)ctx.ir, (int)ctx.cycles);
  if (trace_result != 0) {
    return finalize_break_request(BreakReason::Trace, allow_break);
  }

  // Full instruction hook
  if (_instr_hook) {
    int result = _instr_hook(ctx.pc, ctx.ir, ctx.cycles);
    if (result != 0) {
      return finalize_break_request(BreakReason::InstrHook, allow_break);
    }
  }

  // JS probe + legacy hook (filtered) via unified function
  const int js_result = my_instruction_hook_function(ctx.pc);
  if (js_result != 0) {
    // JS requested a stop; vector to sentinel for deterministic exit
    if (_exec_session.active) {
      m68k_set_reg(M68K_REG_PC, _exec_session.sentinel_pc);
      _exec_session.done = true;
      if (_enable_printf_logging) {
        printf("processHooks: JS break at pc=0x%08X -> sentinel=0x%08X\n",
               ctx.pc, _exec_session.sentinel_pc);
      }
    }
    return finalize_break_request(BreakReason::JsHook, allow_break);
  }

  // End if we hit the sentinel
  if (_exec_session.isSentinelPc(ctx.pc)) {
    _exec_session.done = true;
    _exec_session.markConsumed();
    _last_break_reason = BreakReason::Sentinel;
    if (_enable_printf_logging) {
      printf("processHooks: sentinel pc encountered (pc=0x%08X)\n", ctx.pc);
    }
    if (allow_break) {
      m68k_end_timeslice();
    }
    return HookResult::Break;
  }

  return HookResult::Continue;
}

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
    if (!contains(addr, size)) {
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
    if (!contains(addr, size)) {
      return false;
    }
    unsigned int offset = addr - start_;
    for (int i = 0; i < size; ++i) {
      data_[offset + i] = (value >> ((size - 1 - i) * 8)) & 0xFF;
    }
    return true;
  }

 private:
  bool contains(unsigned int addr, int size) const {
    if (size <= 0) {
      return false;
    }

    const uint64_t request_start = static_cast<uint64_t>(addr);
    const uint64_t request_size = static_cast<uint64_t>(static_cast<unsigned int>(size));
    const uint64_t request_end = request_start + request_size;
    const uint64_t region_start = static_cast<uint64_t>(start_);
    const uint64_t region_end = region_start + static_cast<uint64_t>(size_);

    return request_start >= region_start && request_end <= region_end;
  }
};
static std::vector<Region> _regions;
static std::unordered_map<unsigned int, std::string> _function_names;
static std::unordered_map<unsigned int, std::string> _memory_names;

struct MemoryRangeName {
  unsigned int start;
  unsigned int end;  // inclusive end address within address space bounds
  std::string base_name;
  std::string decorated_label;
};

static std::vector<MemoryRangeName> _memory_ranges;
static std::unordered_map<unsigned int, std::string> _memory_range_cache;

static void invalidate_memory_range_cache(unsigned int start, unsigned int end) {
  if (_memory_range_cache.empty() || start > end) {
    return;
  }

  for (auto it = _memory_range_cache.begin(); it != _memory_range_cache.end();) {
    const unsigned int addr = it->first;
    if (addr >= start && addr <= end) {
      it = _memory_range_cache.erase(it);
    } else {
      ++it;
    }
  }
}

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
    _memory_ranges.clear();
    _memory_range_cache.clear();
    _exec_session = SentinelSession{};
    m68k_fault_clear();
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
    if (!name || size == 0) {
      return;
    }

    const uint64_t start64 = start;
    const uint64_t size64 = size;
    uint64_t end64 = start64 + size64 - 1;
    if (end64 > static_cast<uint64_t>(kAddressSpaceMax)) {
      end64 = static_cast<uint64_t>(kAddressSpaceMax);
    }

    MemoryRangeName range{
      start,
      static_cast<unsigned int>(end64),
      std::string(name),
      std::string(name) + "[" + std::to_string(size) + "]",
    };

    bool replaced = false;
    for (auto& existing : _memory_ranges) {
      if (existing.start == range.start) {
        invalidate_memory_range_cache(existing.start, existing.end);
        existing = range;
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      _memory_ranges.push_back(range);
    }

    invalidate_memory_range_cache(range.start, range.end);

    _memory_names[range.start] = range.decorated_label;

    if (_enable_printf_logging)
      printf(
        "register_memory_range: 0x%08X-0x%08X = '%s'\n",
        range.start,
        range.end,
        range.decorated_label.c_str());
  }

  void clear_registered_names() {
    _function_names.clear();
    _memory_names.clear();
    _memory_ranges.clear();
    _memory_range_cache.clear();
    if (_enable_printf_logging)
      printf("clear_registered_names: cleared all names\n");
  }
  
  const char* get_function_name(unsigned int address) {
    auto it = _function_names.find(address);
    return (it != _function_names.end()) ? it->second.c_str() : nullptr;
  }
  
  const char* get_memory_name(unsigned int address) {
    auto direct = _memory_names.find(address);
    if (direct != _memory_names.end()) {
      return direct->second.c_str();
    }

    auto cached = _memory_range_cache.find(address);
    if (cached != _memory_range_cache.end()) {
      return cached->second.c_str();
    }

    for (const auto& range : _memory_ranges) {
      if (address < range.start || address > range.end) {
        continue;
      }

      if (address == range.start) {
        auto base = _memory_names.find(range.start);
        if (base != _memory_names.end()) {
          return base->second.c_str();
        }
        return range.decorated_label.c_str();
      }

      const unsigned int offset = address - range.start;
      std::ostringstream label;
      label << range.base_name << "+0x" << std::uppercase << std::hex << offset;
      auto& stored = _memory_range_cache[address];
      stored = label.str();
      return stored.c_str();
    }

    return nullptr;
  }

  // Break reason helpers (for tests / debugging)
  int m68k_get_last_break_reason() {
    return static_cast<int>(_last_break_reason);
  }
  void m68k_reset_last_break_reason() {
    _last_break_reason = BreakReason::None;
  }

  // Run until JS-side PC hook requests a stop; when that happens,
  // vector PC to sentinel (max address, even-aligned) and return cycles.
  // timeslice is the cycle budget per m68k_execute() burst.
  unsigned long long m68k_call_until_js_stop(unsigned int entry_pc, unsigned int timeslice) {
    if (timeslice == 0) timeslice = kDefaultTimeslice;
    SessionGuard guard(entry_pc);
    if (_enable_printf_logging) {
      const unsigned int sp_start = m68k_get_reg(nullptr, M68K_REG_SP);
      printf("call_until_js_stop: start pc=0x%08X sp=0x%08X timeslice=%u\n",
             entry_pc, sp_start, timeslice);
    }
    unsigned long long total_cycles = 0;
    unsigned int iter = 0;
    while (!_exec_session.done) {
      total_cycles += m68k_execute(timeslice);
      if (_enable_printf_logging && iter < 16) {
        const unsigned int loop_pc = m68k_get_reg(nullptr, M68K_REG_PC);
        const unsigned int loop_sp = m68k_get_reg(nullptr, M68K_REG_SP);
        printf("call_until_js_stop: iter=%u pc=0x%08X sp=0x%08X done=%d\n",
               iter, loop_pc, loop_sp, _exec_session.done ? 1 : 0);
      }
      ++iter;
    }
    _exec_session.finalize();
    if (_enable_printf_logging) {
      const unsigned int sp_end = m68k_get_reg(nullptr, M68K_REG_SP);
      const unsigned int pc_end = m68k_get_reg(nullptr, M68K_REG_PC);
      printf("call_until_js_stop: exit pc=0x%08X sp=0x%08X cycles=%llu reason=%d\n",
             pc_end, sp_end, total_cycles, static_cast<int>(_last_break_reason));
    }
    return total_cycles;
  }

  // Execute exactly one instruction and return the cycles consumed.
  unsigned long long m68k_step_one(void) {
    // Capture start PC for accurate boundary normalization
    const unsigned int start_pc = m68k_get_reg(nullptr, M68K_REG_PC);
    _step_state = StepState::Arm;
    unsigned long long cycles = m68k_execute(kDefaultTimeslice);
    // If CPU became stopped before next hook (e.g., STOP), ensure clean state
    if (_step_state != StepState::Idle) {
      _step_state = StepState::Idle;
    }

    // Determine normalized end-of-instruction PC.
    // Default to the start_pc plus decoded size (fall-through), but if
    // core PC indicates a control-flow change, base normalization on that.
    unsigned int new_pc = m68k_get_reg(nullptr, M68K_REG_PC);
    {
      char tmp[256];
      const unsigned int size = m68k_disassemble(tmp, start_pc, M68K_CPU_TYPE_68000);
      if (size > 0) {
        const unsigned int fallthrough_end = start_pc + size;
        if (new_pc == fallthrough_end || new_pc == fallthrough_end + 2) {
          // No control-flow change; fix prefetch drift to the exact boundary
          new_pc = fallthrough_end;
        } else {
          // Control-flow changed (branch/jsr/jmp/exception). m68k_execute() returned
          // after fetching the first word of the NEXT instruction, so PC advanced by 2.
          // Normalize by undoing the one-word prefetch so PC reflects the true next PC.
          if (new_pc >= 2) new_pc -= 2;
        }
      } else {
        // If disassembly fails, still try to undo one-word prefetch drift safely.
        if (new_pc >= 2) new_pc -= 2;
      }
    }

    // Normalize PPC to the start of the stepped instruction for stable semantics
    m68k_set_reg(M68K_REG_PPC, start_pc);
    // Normalize PC to computed next-instruction boundary
    m68k_set_reg(M68K_REG_PC, new_pc);

    return cycles;
  }
  
  /* (Removed register accessor helpers; use m68k_get_reg/m68k_set_reg from TS) */

  // Resolve register enum value by name. Returns -1 if unknown.
  // Recognizes: D0-D7, A0-A7, PC, SR, SP, PPC, USP, ISP, MSP, SFC, DFC,
  // VBR, CACR, CAAR, PREF_ADDR/PREFADDR, PREF_DATA/PREFDATA, IR, CPU_TYPE/CPUTYPE
  int m68k_regnum_from_name(const char* name) {
    if (!name) return -1;
    // Normalize to uppercase and remove spaces
    std::string s;
    for (const char* p = name; *p; ++p) {
      char c = *p;
      if (c >= 'a' && c <= 'z') c = static_cast<char>(c - ('a' - 'A'));
      if (c == ' ') continue;
      s.push_back(c);
    }
    if (s.size() == 2 && s[0] == 'D' && s[1] >= '0' && s[1] <= '7') {
      return static_cast<int>(M68K_REG_D0 + (s[1] - '0'));
    }
    if (s.size() == 2 && s[0] == 'A' && s[1] >= '0' && s[1] <= '7') {
      return static_cast<int>(M68K_REG_A0 + (s[1] - '0'));
    }
    if (s == "PC") return static_cast<int>(M68K_REG_PC);
    if (s == "SR") return static_cast<int>(M68K_REG_SR);
    if (s == "SP") return static_cast<int>(M68K_REG_SP);
    if (s == "PPC") return static_cast<int>(M68K_REG_PPC);
    if (s == "USP") return static_cast<int>(M68K_REG_USP);
    if (s == "ISP") return static_cast<int>(M68K_REG_ISP);
    if (s == "MSP") return static_cast<int>(M68K_REG_MSP);
    if (s == "SFC") return static_cast<int>(M68K_REG_SFC);
    if (s == "DFC") return static_cast<int>(M68K_REG_DFC);
    if (s == "VBR") return static_cast<int>(M68K_REG_VBR);
    if (s == "CACR") return static_cast<int>(M68K_REG_CACR);
    if (s == "CAAR") return static_cast<int>(M68K_REG_CAAR);
    if (s == "PREF_ADDR" || s == "PREFADDR") return static_cast<int>(M68K_REG_PREF_ADDR);
    if (s == "PREF_DATA" || s == "PREFDATA") return static_cast<int>(M68K_REG_PREF_DATA);
    if (s == "IR") return static_cast<int>(M68K_REG_IR);
    if (s == "CPU_TYPE" || s == "CPUTYPE") return static_cast<int>(M68K_REG_CPU_TYPE);
    return -1;
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

// Helper: whether to invoke legacy PC hook for given pc based on filter set
static inline bool should_invoke_pc_hook(unsigned int pc) {
  if (_pc_hook_addrs.empty()) return true; // backward compatible: hook all
  return _pc_hook_addrs.find(pc) != _pc_hook_addrs.end();
}

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
    if (should_invoke_pc_hook(pc)) {
      int js_result = js_probe_callback(pc);
      if (js_result != 0) return js_result;  // JS wants to break
    }
  }

  // Call legacy PC hook if present and allowed by filter
  if (_pc_hook && should_invoke_pc_hook(pc)) {
    return _pc_hook(pc);
  }

  return 0;
}

// (Removed _pc_hook_filtering_aware in favor of should_invoke_pc_hook + direct call)

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
    HookContext ctx{pc, ir, cycles};
    return static_cast<int>(processHooks(ctx, /*allow_break=*/false));
#else
    HookContext ctx{pc, ir, cycles};
    return static_cast<int>(processHooks(ctx, /*allow_break=*/true));
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

  void perfetto_enable_instruction_registers(int enable) {
    if (_enable_printf_logging)
      printf("perfetto_enable_instruction_registers: %d\n", enable);
    m68k_perfetto_enable_instruction_registers(enable);
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
