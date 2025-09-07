#include <stdio.h>
#include <stdint.h>
#include "m68k.h"

static uint8_t memory[1024*1024];

static unsigned int read_memory_8(unsigned int address) {
    return memory[address & 0xFFFFF];
}

static unsigned int read_memory_16(unsigned int address) {
    return (memory[address & 0xFFFFF] << 8) | memory[(address + 1) & 0xFFFFF];
}

static unsigned int read_memory_32(unsigned int address) {
    return (read_memory_16(address) << 16) | read_memory_16(address + 2);
}

static void write_memory_8(unsigned int address, unsigned int value) {
    memory[address & 0xFFFFF] = value;
}

static void write_memory_16(unsigned int address, unsigned int value) {
    memory[address & 0xFFFFF] = (value >> 8) & 0xFF;
    memory[(address + 1) & 0xFFFFF] = value & 0xFF;
}

static void write_memory_32(unsigned int address, unsigned int value) {
    write_memory_16(address, (value >> 16) & 0xFFFF);
    write_memory_16(address + 2, value & 0xFFFF);
}

static void write_word(unsigned int address, unsigned int value) {
    write_memory_16(address, value);
}

int main() {
    /* Initialize M68K */
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_init();
    
    /* Set up memory callbacks */
    m68k_set_read_memory_8_callback(read_memory_8);
    m68k_set_read_memory_16_callback(read_memory_16);
    m68k_set_read_memory_32_callback(read_memory_32);
    m68k_set_write_memory_8_callback(write_memory_8);
    m68k_set_write_memory_16_callback(write_memory_16);
    m68k_set_write_memory_32_callback(write_memory_32);
    
    /* Set up exception vectors */
    write_memory_32(0x00, 0x100000);  /* Initial SSP */
    write_memory_32(0x04, 0x1000);    /* Initial PC */
    write_memory_32(0x20, 0x2060);    /* Privilege violation vector */
    
    /* Set up privilege violation handler (just RTE) */
    write_word(0x2060, 0x4E73); /* RTE */
    
    /* Set up test program */
    write_word(0x1000, 0x4E72); /* STOP instruction (privileged) */
    write_word(0x1002, 0x2700); /* STOP parameter */
    write_word(0x1004, 0x4E71); /* NOP */
    
    /* Reset CPU */
    m68k_pulse_reset();
    
    printf("Initial state:\n");
    printf("  PC: 0x%08X\n", m68k_get_reg(NULL, M68K_REG_PC));
    printf("  SR: 0x%04X (S=%d)\n", m68k_get_reg(NULL, M68K_REG_SR),
           (m68k_get_reg(NULL, M68K_REG_SR) & 0x2000) ? 1 : 0);
    printf("  SP: 0x%08X\n", m68k_get_reg(NULL, M68K_REG_SP));
    
    /* Switch to user mode */
    unsigned int sr = m68k_get_reg(NULL, M68K_REG_SR);
    sr &= ~0x2000; /* Clear supervisor bit */
    m68k_set_reg(M68K_REG_SR, sr);
    
    printf("\nAfter switching to user mode:\n");
    printf("  PC: 0x%08X\n", m68k_get_reg(NULL, M68K_REG_PC));
    printf("  SR: 0x%04X (S=%d)\n", m68k_get_reg(NULL, M68K_REG_SR),
           (m68k_get_reg(NULL, M68K_REG_SR) & 0x2000) ? 1 : 0);
    printf("  SP: 0x%08X\n", m68k_get_reg(NULL, M68K_REG_SP));
    
    /* Check what's on the supervisor stack */
    printf("\nSupervisor stack contents:\n");
    printf("  SSP: 0x%08X\n", m68k_get_reg(NULL, M68K_REG_SSP));
    printf("  [SSP-6]: 0x%04X (SR)\n", read_memory_16(m68k_get_reg(NULL, M68K_REG_SSP) - 6));
    printf("  [SSP-4]: 0x%08X (PC)\n", read_memory_32(m68k_get_reg(NULL, M68K_REG_SSP) - 4));
    
    /* Execute - should trigger privilege violation */
    printf("\nExecuting privileged instruction...\n");
    m68k_execute(100);
    
    printf("\nAfter exception and RTE:\n");
    printf("  PC: 0x%08X\n", m68k_get_reg(NULL, M68K_REG_PC));
    printf("  SR: 0x%04X (S=%d)\n", m68k_get_reg(NULL, M68K_REG_SR),
           (m68k_get_reg(NULL, M68K_REG_SR) & 0x2000) ? 1 : 0);
    printf("  SP: 0x%08X\n", m68k_get_reg(NULL, M68K_REG_SP));
    
    return 0;
}

