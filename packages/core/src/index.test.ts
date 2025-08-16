// ESM-compatible test file with proper mocking
import { jest } from '@jest/globals';
import type { System } from './types.js';

let createSystem: (cfg: any) => Promise<System>;

beforeAll(async () => {
  // Mock BEFORE importing the SUT using ESM-compatible API
  // Map to .js specifier; jest will route it to the .ts file via moduleNameMapper
  await jest.unstable_mockModule('./musashi-wrapper.js', async () => {
    // Re-export the mock module's actual ESM exports
    return await import('./__mocks__/musashi-wrapper.js');
  });

  // Now import the SUT (after the mock is in place)
  ({ createSystem } = await import('./index.js'));
});

describe('@m68k/core', () => {
  let system: System;

  beforeEach(async () => {
    // Create a simple test ROM with some basic instructions
    // Must be at least 0x412 bytes to hold test program at 0x400
    const rom = new Uint8Array(0x1000); // 4KB ROM
    
    // Set reset vectors
    // Stack pointer at 0x10000
    rom[0] = 0x00;
    rom[1] = 0x01;
    rom[2] = 0x00;
    rom[3] = 0x00;
    
    // Program counter at 0x400
    rom[4] = 0x00;
    rom[5] = 0x00;
    rom[6] = 0x04;
    rom[7] = 0x00;
    
    // Simple test program at 0x400
    // MOVE.L #$12345678, D0
    rom[0x400] = 0x20;
    rom[0x401] = 0x3C;
    rom[0x402] = 0x12;
    rom[0x403] = 0x34;
    rom[0x404] = 0x56;
    rom[0x405] = 0x78;
    
    // MOVE.L D0, (A0)
    rom[0x406] = 0x20;
    rom[0x407] = 0x80;
    
    // ADD.L #1, D1
    rom[0x408] = 0x06;
    rom[0x409] = 0x81;
    rom[0x40A] = 0x00;
    rom[0x40B] = 0x00;
    rom[0x40C] = 0x00;
    rom[0x40D] = 0x01;
    
    // RTS
    rom[0x40E] = 0x4E;
    rom[0x40F] = 0x75;
    
    system = await createSystem({
      rom,
      ramSize: 0x1000
    });
  });

  afterEach(() => {
    // No cleanup method in System interface
  });

  it('should create a system', () => {
    expect(system).toBeDefined();
    expect(system.run).toBeDefined();
    expect(system.call).toBeDefined();
    expect(system.reset).toBeDefined();
    expect(system.read).toBeDefined();
    expect(system.write).toBeDefined();
  });

  it('should read and write memory', () => {
    const ramBase = 0x100000;
    
    // Verify initial memory is zero
    const initialValue = system.read(ramBase, 4);
    expect(initialValue).toBe(0);
    
    // Write a 32-bit value
    system.write(ramBase, 4, 0x12345678);
    
    // Read it back
    const value = system.read(ramBase, 4);
    expect(value).toBe(0x12345678);
    
    // Test byte access
    system.write(ramBase + 4, 1, 0xAB);
    expect(system.read(ramBase + 4, 1)).toBe(0xAB);
    
    // Test word access
    system.write(ramBase + 6, 2, 0xCDEF);
    expect(system.read(ramBase + 6, 2)).toBe(0xCDEF);
  });

  it('should read and write byte arrays', () => {
    const ramBase = 0x100000;
    const data = new Uint8Array([0x01, 0x02, 0x03, 0x04, 0x05]);
    
    // Write array
    system.writeBytes(ramBase, data);
    
    // Read it back
    const readData = system.readBytes(ramBase, 5);
    expect(readData).toEqual(data);
  });

  it('should execute simple instructions', async () => {
    // Execute a few cycles
    const cycles = await system.run(100);
    
    // Should have executed something
    expect(cycles).toBeGreaterThan(0);
    
    // PC should have advanced from 0x400
    const pc = system.getRegisters().pc;
    expect(pc).not.toBe(0x400);
  });

  it('should get and set registers', () => {
    const regs = system.getRegisters();
    expect(regs).toBeDefined();
    
    // Set D0
    system.setRegister('d0', 0x12345678);
    expect(system.getRegisters().d0).toBe(0x12345678);
    
    // Set A0
    system.setRegister('a0', 0x100000);
    expect(system.getRegisters().a0).toBe(0x100000);
    
    // Set multiple registers
    system.setRegister('d1', 0x11111111);
    system.setRegister('d2', 0x22222222);
    system.setRegister('a1', 0x100100);
    
    const newRegs = system.getRegisters();
    expect(newRegs.d1).toBe(0x11111111);
    expect(newRegs.d2).toBe(0x22222222);
    expect(newRegs.a1).toBe(0x100100);
  });

  it('should support probe hooks', async () => {
    const addresses: number[] = [];
    
    // Initial PC should be 0x400 from reset vector
    const initialPc = system.getRegisters().pc;
    expect(initialPc).toBe(0x400);
    
    const removeHook = system.probe(0x400, (sys) => {
      addresses.push(sys.getRegisters().pc);
    });
    
    // Execute some instructions
    await system.run(50);
    
    // Should have called the hook for the probe address
    expect(addresses.length).toBeGreaterThan(0);
    expect(addresses[0]).toBe(0x400);
    
    // Clean up
    removeHook();
  });

  it('should check tracer availability', () => {
    const tracer = system.tracer;
    expect(tracer).toBeDefined();
    
    // Mock doesn't have Perfetto
    expect(tracer.isAvailable()).toBe(false);
  });

  it('should handle tracer when not available', () => {
    const tracer = system.tracer;
    
    // Should throw when trying to start without Perfetto
    expect(() => {
      tracer.start({ flow: true });
    }).toThrow('Perfetto tracing is not available');
  });

  it('should register symbol names without crashing', () => {
    // Should not throw
    expect(() => {
      system.tracer.registerFunctionNames({
        0x400: 'main',
        0x500: 'helper'
      });
      system.tracer.registerMemoryNames({
        0x100000: 'buffer',
        0x100100: 'data'
      });
    }).not.toThrow();
  });
});