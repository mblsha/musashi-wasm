import { createSystem, System } from './index';

describe('@m68k/core', () => {
  let system: System;

  beforeEach(async () => {
    // Create a simple test ROM with some basic instructions
    const rom = new Uint8Array(1024);
    
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
    
    // MOVE.L #$ABCDEF00, D1
    rom[0x406] = 0x22;
    rom[0x407] = 0x3C;
    rom[0x408] = 0xAB;
    rom[0x409] = 0xCD;
    rom[0x40A] = 0xEF;
    rom[0x40B] = 0x00;
    
    // ADD.L D1, D0
    rom[0x40C] = 0xD0;
    rom[0x40D] = 0x81;
    
    // STOP #$2700
    rom[0x40E] = 0x4E;
    rom[0x40F] = 0x72;
    rom[0x410] = 0x27;
    rom[0x411] = 0x00;
    
    system = await createSystem({
      rom,
      ramSize: 64 * 1024
    });
  });

  afterEach(() => {
    if (system && (system as any).cleanup) {
      (system as any).cleanup();
    }
  });

  test('should create a system', () => {
    expect(system).toBeDefined();
    expect(system.read).toBeDefined();
    expect(system.write).toBeDefined();
    expect(system.getRegisters).toBeDefined();
  });

  test('should read and write memory', () => {
    const ramBase = 0x100000;
    
    // Write a value to RAM
    system.write(ramBase, 4, 0x12345678);
    
    // Read it back
    const value = system.read(ramBase, 4);
    expect(value).toBe(0x12345678);
    
    // Test byte access
    system.write(ramBase + 4, 1, 0xAB);
    expect(system.read(ramBase + 4, 1)).toBe(0xAB);
    
    // Test word access
    system.write(ramBase + 8, 2, 0xCDEF);
    expect(system.read(ramBase + 8, 2)).toBe(0xCDEF);
  });

  test('should read and write byte arrays', () => {
    const ramBase = 0x100000;
    const testData = new Uint8Array([0x01, 0x02, 0x03, 0x04, 0x05]);
    
    // Write bytes
    system.writeBytes(ramBase, testData);
    
    // Read them back
    const readData = system.readBytes(ramBase, testData.length);
    expect(readData).toEqual(testData);
  });

  test('should execute simple instructions', async () => {
    // Reset the CPU
    system.reset();
    
    // Run for a limited number of cycles
    const cycles = await system.run(100);
    expect(cycles).toBeGreaterThan(0);
    
    // Check that D0 contains the expected result
    const regs = system.getRegisters();
    const expectedResult = 0x12345678 + 0xABCDEF00;
    expect(regs.d0).toBe(expectedResult >>> 0); // Ensure unsigned comparison
  });

  test('should get and set registers', () => {
    // Set some register values
    system.setRegister('d0', 0x12345678);
    system.setRegister('d1', 0xABCDEF00);
    system.setRegister('a0', 0x00100000);
    
    // Read them back
    const regs = system.getRegisters();
    expect(regs.d0).toBe(0x12345678);
    expect(regs.d1).toBe(0xABCDEF00);
    expect(regs.a0).toBe(0x00100000);
  });

  test('should support probe hooks', async () => {
    let hookCalled = false;
    let hookPC = 0;
    
    // Add a probe at the start of our program
    const removeHook = system.probe(0x400, (sys) => {
      hookCalled = true;
      hookPC = sys.getRegisters().pc;
    });
    
    // Reset and run
    system.reset();
    await system.run(10);
    
    // Check that the hook was called
    expect(hookCalled).toBe(true);
    expect(hookPC).toBe(0x400);
    
    // Remove the hook
    removeHook();
  });

  test('should check tracer availability', () => {
    expect(system.tracer).toBeDefined();
    expect(system.tracer.isAvailable).toBeDefined();
    
    // Note: Tracer may or may not be available depending on build
    const available = system.tracer.isAvailable();
    expect(typeof available).toBe('boolean');
  });

  test('should handle tracer when not available', () => {
    if (!system.tracer.isAvailable()) {
      expect(() => {
        system.tracer.start({ instructions: true });
      }).toThrow('Perfetto tracing is not available');
    }
  });

  test('should register symbol names without crashing', () => {
    // Even if tracing is not available, these should not crash
    system.tracer.registerFunctionNames({
      0x400: 'main',
      0x500: 'subroutine'
    });
    
    system.tracer.registerMemoryNames({
      0x100000: 'ram_start',
      0x110000: 'stack'
    });
    
    // No crash = success
    expect(true).toBe(true);
  });
});