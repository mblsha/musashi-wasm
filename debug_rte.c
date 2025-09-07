#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "m68k.h"

static uint32_t memory[0x100000/4];  // 1MB memory

/* Memory access callbacks */
unsigned int read_memory_8(unsigned int address) {
    return ((uint8_t*)memory)[address & 0xFFFFF];
}

unsigned int read_memory_16(unsigned int address) {
    address &= 0xFFFFF;
    return (read_memory_8(address) << 8) | read_memory_8(address + 1);
}

unsigned int read_memory_32(unsigned int address) {
    address &= 0xFFFFF;
    return (read_memory_16(address) << 16) | read_memory_16(address + 2);
}

void write_memory_8(unsigned int address, unsigned int value) {
    ((uint8_t*)memory)[address & 0xFFFFF] = value;
}

void write_memory_16(unsigned int address, unsigned int value) {
    address &= 0xFFFFF;
    write_memory_8(address, (value >> 8) & 0xFF);
    write_memory_8(address + 1, value & 0xFF);
}

void write_memory_32(unsigned int address, unsigned int value) {
    address &= 0xFFFFF;
    write_memory_16(address, (value >> 16) & 0xFFFF);
    write_memory_16(address + 2, value & 0xFFFF);
}

int main() {
    memset(memory, 0, sizeof(memory));
    
    /* Initialize CPU */
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    
    /* Set up reset vector */
    write_memory_32(0, 0x100000);  /* Initial SP */
    write_memory_32(4, 0x1000);    /* Initial PC */
    
    /* Set up illegal instruction vector */
    write_memory_32(4 * 4, 0x2000);  /* Vector 4: Illegal instruction */
    
    /* Program: Illegal instruction at 0x1000 */
    write_memory_16(0x1000, 0xFFFF);  /* Illegal instruction */
    write_memory_16(0x1002, 0x4E71);  /* NOP (should execute after exception) */
    
    /* Exception handler at 0x2000 */
    write_memory_16(0x2000, 0x7001);  /* MOVEQ #1, D0 */
    write_memory_16(0x2002, 0x4E73);  /* RTE */
    
    /* Reset CPU */
    m68k_pulse_reset();
    
    /* Execute with debug output */
    printf("Initial state:\n");
    printf("  PC: 0x%08X\n", m68k_get_reg(NULL, M68K_REG_PC));
    printf("  SP: 0x%08X\n", m68k_get_reg(NULL, M68K_REG_SP));
    printf("  SR: 0x%04X (S=%d)\n", m68k_get_reg(NULL, M68K_REG_SR),
           (m68k_get_reg(NULL, M68K_REG_SR) & 0x2000) ? 1 : 0);
    printf("  D0: 0x%08X\n", m68k_get_reg(NULL, M68K_REG_D0));
    
    printf("\nExecuting (should hit illegal instruction)...\n");
    m68k_execute(100);
    
    printf("\nAfter execution:\n");
    printf("  PC: 0x%08X\n", m68k_get_reg(NULL, M68K_REG_PC));
    printf("  SP: 0x%08X\n", m68k_get_reg(NULL, M68K_REG_SP));
    printf("  SR: 0x%04X (S=%d)\n", m68k_get_reg(NULL, M68K_REG_SR),
           (m68k_get_reg(NULL, M68K_REG_SR) & 0x2000) ? 1 : 0);
    printf("  D0: 0x%08X\n", m68k_get_reg(NULL, M68K_REG_D0));
    
    /* Check stack contents */
    uint32_t sp = m68k_get_reg(NULL, M68K_REG_SP);
    printf("\nStack contents at SP (0x%08X):\n", sp);
    for (int i = 0; i < 16; i += 2) {
        printf("  [SP+%d]: 0x%04X\n", i, read_memory_16(sp + i));
    }
    
    if (m68k_get_reg(NULL, M68K_REG_D0) == 1 && m68k_get_reg(NULL, M68K_REG_PC) == 0x1002) {
        printf("\nSUCCESS: Exception handled correctly and returned to 0x1002\n");
    } else {
        printf("\nFAILURE: Did not return correctly from exception\n");
    }
    
    return 0;
}