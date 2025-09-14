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
    m68k_set_reg(M68K_REG_PC, entry_pc);
  }
  void finish() { active = false; }
  bool isSentinelPc(unsigned int pc) const {
    return active && AddrPolicy32::matches(pc, sentinel_pc);
  }

  bool active = false;
  bool done = false;
  unsigned int sentinel_pc = kAddressSpaceMax - 1;
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
    _last_break_reason = BreakReason::Trace;
    if (allow_break) {
      m68k_end_timeslice();
      return HookResult::Break;
    }
    return HookResult::Continue;
  }

  // Full instruction hook
  if (_instr_hook) {
    int result = _instr_hook(ctx.pc, ctx.ir, ctx.cycles);
    if (result != 0) {
      _last_break_reason = BreakReason::InstrHook;
      if (allow_break) {
        m68k_end_timeslice();
        return HookResult::Break;
      }
      return HookResult::Continue;
    }
  }

  // JS probe + legacy hook (filtered) via unified function
  const int js_result = my_instruction_hook_function(ctx.pc);
  if (js_result != 0) {
    // JS requested a stop; vector to sentinel for deterministic exit
    if (_exec_session.active) {
      m68k_set_reg(M68K_REG_PC, _exec_session.sentinel_pc);
      _exec_session.done = true;
    }
    _last_break_reason = BreakReason::JsHook;
    if (allow_break) {
      m68k_end_timeslice();
      return HookResult::Break;
    }
    return HookResult::Continue;
  }

  // End if we hit the sentinel
  if (_exec_session.isSentinelPc(ctx.pc)) {
    _exec_session.done = true;
    _last_break_reason = BreakReason::Sentinel;
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
    _exec_session = SentinelSession{};
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
    unsigned long long total_cycles = 0;
    while (!_exec_session.done) {
      total_cycles += m68k_execute(timeslice);
    }
    return total_cycles;
  }

  // Execute exactly one instruction and return the cycles consumed.
  unsigned long long m68k_step_one(void) {
    // Capture start PC for accurate endPc normalization
    unsigned int start_pc = m68k_get_reg(nullptr, M68K_REG_PC);
    _step_state = StepState::Arm;
    unsigned long long cycles = m68k_execute(kDefaultTimeslice);
    // If CPU became stopped before next hook (e.g., STOP), ensure clean state
    if (_step_state != StepState::Idle) {
      _step_state = StepState::Idle;
    }
    /*
     * Why we normalize PC/PPC here (detailed rationale):
     *
     * The Musashi main loop (m68kcpu.c) performs these steps each iteration:
     *   1) Set REG_PPC = REG_PC (record previous PC at top of iteration)
     *   2) Fetch next instruction word: REG_IR = m68ki_read_imm_16();
     *      This fetch advances REG_PC by 2 bytes (prefetch)
     *   3) Invoke the instruction hook: m68ki_instr_hook(REG_PPC, REG_IR, executed_cycles)
     *   4) Execute the instruction body
     *   5) At the end of the iteration, the loop may set REG_PPC = REG_PC again for the next entry
     *
     * Our single-step mechanism (this function) arms a state machine that says:
     *   - On the first instruction hook we see, "arm" a break for the NEXT hook, and
     *     allow the current instruction to run to completion.
     *   - On the following iteration's hook, request an immediate timeslice end.
     *
     * Subtle effect without normalization:
     *   After finishing instruction #1, the loop begins iteration for instruction #2.
     *   At the TOP of that iteration, the core:
     *     - sets REG_PPC = REG_PC (which now reflects the PC after instruction #1), and
     *     - fetches the first word of instruction #2, advancing REG_PC by 2 (prefetch).
     *   Then the hook for instruction #2 fires and our step state requests a stop. As a result,
     *   when m68k_execute() returns, REG_PC has already been advanced by the prefetch of the NEXT
     *   instruction, and REG_PPC reflects the post-instruction-1 PC (not the start of instruction #1).
     *
     * Concrete example (what the failing test observed before this fix):
     *   - Program at 0x400: MOVE.L #imm,D0 (6 bytes), then NOP
     *   - True post-instruction boundary is 0x406. However, because the next iteration prefetches
     *     before we stop, REG_PC ends up at 0x408 (0x406 + 2); REG_PPC no longer equals 0x400.
     *   - Tests that assert endPc == startPc + decodedSize (and PPC == startPc) fail by 2 bytes.
     *
     * To provide a stable "execute exactly one instruction" contract, we normalize the architectural
     * state here to the post-instruction boundary based on the disassembler’s size:
     *   - Decode at the original start_pc to obtain the instruction size
     *   - Force REG_PC = start_pc + size (undoing the prefetch drift)
     *   - Force REG_PPC = start_pc so metadata/consumers see the correct previous PC
     *
     * Notes:
     *   - We only adjust PC/PPC (metadata-visible architectural state). We do not modify cycles; the
     *     returned cycle count remains whatever the core executed.
     *   - If decoding were to fail (size == 0), we leave PC as-is to avoid guessing.
     */
    {
      // m68k_disassemble writes a human-readable string into the provided
      // buffer and returns the instruction size. Provide a sufficiently
      // large buffer to avoid overwriting (sanitizers would flag too-small).
      char tmp[256];
      unsigned int size = m68k_disassemble(tmp, start_pc, M68K_CPU_TYPE_68000);
      if (size > 0) {
        unsigned int normalized_end = start_pc + size;
        m68k_set_reg(M68K_REG_PC, normalized_end);
        // Keep PPC consistent: previous PC should reflect the instruction start
        m68k_set_reg(M68K_REG_PPC, start_pc);
      }
    }
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
