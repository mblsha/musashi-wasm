#include <stdio.h>
#include "m68k.h"

typedef int (*read_mem_t)(unsigned int address, int size);
typedef void (*write_mem_t)(unsigned int address, int size, unsigned int value);
typedef int (*pc_hook_t)(unsigned int pc);

static read_mem_t _read_mem = 0;
static write_mem_t _write_mem = 0;
static pc_hook_t _pc_hook = 0;

void set_read_mem_func(read_mem_t func) {
  printf("set_read_mem_func: %p\n", (void*)func);
   _read_mem = func;
   }
void set_write_mem_func(write_mem_t func) {
  printf("set_write_mem_func: %p\n", (void*)func);
  _write_mem = func; }
void set_pc_hook_func(pc_hook_t func) {
  printf("set_pc_hook_func: %p\n", (void*)func);
  _pc_hook = func; }

unsigned int m68k_read_memory_8(unsigned int address) { return _read_mem(address, 1); }
unsigned int m68k_read_memory_16(unsigned int address) { return _read_mem(address, 2); }
unsigned int m68k_read_memory_32(unsigned int address) { return _read_mem(address, 4); }

void m68k_write_memory_8(unsigned int address, unsigned int value) { _write_mem(address, 1, value); }
void m68k_write_memory_16(unsigned int address, unsigned int value) { _write_mem(address, 2, value); }
void m68k_write_memory_32(unsigned int address, unsigned int value) { _write_mem(address, 4, value); }

int my_instruction_hook_function(unsigned int pc) {
  return _pc_hook(pc);
  // m68k_end_timeslice();
  // m68k_pulse_halt();
}
