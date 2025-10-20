#include "musashi_fault.h"
#include "m68kcpu.h"

static musashi_fault_record_t g_fault_record;

void m68k_fault_clear(void) {
  g_fault_record.active = 0;
}

musashi_fault_record_t* m68k_fault_record_ptr(void) {
  return &g_fault_record;
}

void m68k_fault_capture(musashi_fault_kind_t kind,
                        uint32_t vector,
                        uint32_t address,
                        uint32_t size,
                        uint32_t extra) {
  g_fault_record.active = 1;
  g_fault_record.kind = (uint32_t)kind;
  g_fault_record.vector = vector;
  g_fault_record.address = address;
  g_fault_record.size = size;
  g_fault_record.pc = m68k_get_reg(NULL, M68K_REG_PC);
  g_fault_record.ppc = m68k_get_reg(NULL, M68K_REG_PPC);
  g_fault_record.sp = m68k_get_reg(NULL, M68K_REG_SP);
  g_fault_record.sr = m68k_get_reg(NULL, M68K_REG_SR);
  g_fault_record.opcode = m68k_get_reg(NULL, M68K_REG_IR);
  g_fault_record.extra = extra;
}
