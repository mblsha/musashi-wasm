// ESM-compatible test file using real WASM
import { createSystem } from './index.js';
import type { System } from './types.js';

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
    rom[0x401] = 0x3c;
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
    rom[0x40a] = 0x00;
    rom[0x40b] = 0x00;
    rom[0x40c] = 0x00;
    rom[0x40d] = 0x01;

    // RTS
    rom[0x40e] = 0x4e;
    rom[0x40f] = 0x75;

    system = await createSystem({
      rom,
      ramSize: 0x1000,
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
    system.write(ramBase + 4, 1, 0xab);
    expect(system.read(ramBase + 4, 1)).toBe(0xab);

    // Test word access
    system.write(ramBase + 6, 2, 0xcdef);
    expect(system.read(ramBase + 6, 2)).toBe(0xcdef);
  });

  it('respects memory bounds for multi-byte access', () => {
    const ramBase = 0x100000;
    const ramSize = 0x1000;

    // Single byte at last address should work
    system.write(ramBase + ramSize - 1, 1, 0xAB);
    expect(system.read(ramBase + ramSize - 1, 1)).toBe(0xAB);

    // Multi-byte read/write crossing end should be safely ignored / return 0
    system.write(ramBase + ramSize - 1, 2, 0xCDEF);
    expect(system.read(ramBase + ramSize - 1, 2)).toBe(0);

    system.write(ramBase + ramSize - 2, 4, 0x11223344);
    expect(system.read(ramBase + ramSize - 2, 4)).toBe(0);
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

    const removeHook = system.probe(0x400, sys => {
      addresses.push(sys.getRegisters().pc);
    });

    // Execute some instructions
    await system.run(50);

    // Should have called the hook for the probe address
    expect(addresses.length).toBeGreaterThan(0);
    // Hook is called after instruction execution, so PC will be after 0x400
    expect(addresses[0]).toBeGreaterThan(0x400);

    // Clean up
    removeHook();
  });

  it('should override execution at a given address', async () => {
    const calls: number[] = [];
    const overrideAddress = 0x408; // ADD.L #1, D1 instruction

    const removeOverride = system.override(overrideAddress, sys => {
      calls.push(sys.getRegisters().pc);
      // Custom behavior: set D1 and jump to RTS
      sys.setRegister('d1', 0x42);
      sys.setRegister('pc', 0x40e); // Address of RTS instruction
    });

    await system.run(100);

    expect(calls.length).toBe(1);
    expect(system.getRegisters().d1).toBe(0x42);
    expect(system.getRegisters().pc).toBe(0x40e);

    removeOverride();
  });

  it('call() via C++ session stops when override PC is hit', async () => {
    const subAddr = 0x410;
    // Build a tiny subroutine at 0x410: MOVE.L #$CAFEBABE,D2 ; RTS
    const sub = new Uint8Array([
      0x24, 0x3c, // MOVE.L #imm, D2
      0xca, 0xfe, 0xba, 0xbe, // imm32
      0x4e, 0x75, // RTS
    ]);
    system.writeBytes(subAddr, sub);

    // Register override at RTS PC so the C++ session will stop
    const rtsPc = subAddr + 6;
    const removeOverride = system.override(rtsPc, _ => {
      // Do not modify PC; just request stop
      // Returning true triggers C++ sentinel redirection
    });

    const cycles = await system.call(subAddr);
    expect(cycles).toBeGreaterThan(0);
    expect(system.getRegisters().d2 >>> 0).toBe(0xcafebabe);

    // Verify PC parked at sentinel (accept 24-bit or full 32-bit even)
    const pc = system.getRegisters().pc >>> 0;
    const sentinel24 = 0x00fffffe >>> 0;
    const sentinel32 = 0xfffffffe >>> 0;
    expect([sentinel24, sentinel32]).toContain(pc);

    removeOverride();
  });

  it('call() handles nested subcalls and stops only at outer RTS', async () => {
    // Sub B at 0x520: MOVE.L #$DEADBEEF,D2 ; RTS
    const subB = new Uint8Array([
      0x24, 0x3c,
      0xde, 0xad, 0xbe, 0xef,
      0x4e, 0x75,
    ]);
    system.writeBytes(0x520, subB);

    // Sub A at 0x500: JSR 0x520 ; ADD.L #1,D2 ; RTS
    // 0x4E B9 addr32 = JSR absolute long
    const subA = new Uint8Array([
      0x4e, 0xb9, 0x00, 0x00, 0x05, 0x20, // JSR $00000520
      0x06, 0x82, 0x00, 0x00, 0x00, 0x01, // ADD.L #1, D2
      0x4e, 0x75, // RTS
    ]);
    system.writeBytes(0x500, subA);

    // Stop only at A's RTS
    const outerRts = 0x500 + 6 + 6; // after JSR(6) + ADD.L(6)
    const removeOverride = system.override(outerRts, _ => {
      // no-op, just trigger stop
    });

    const cycles = await system.call(0x500);
    expect(cycles).toBeGreaterThan(0);
    // D2 should be DEADBEEF + 1 from ADD.L
    expect(system.getRegisters().d2 >>> 0).toBe(0xdeadbeef + 1 >>> 0);

    // Sentinel check (accept 24-bit or full 32-bit even)
    const pc = system.getRegisters().pc >>> 0;
    const sentinel24 = 0x00fffffe >>> 0;
    const sentinel32 = 0xfffffffe >>> 0;
    expect([sentinel24, sentinel32]).toContain(pc);

    removeOverride();
  });

  it('should check tracer availability', () => {
    const tracer = system.tracer;
    expect(tracer).toBeDefined();

    // Real WASM may or may not have Perfetto
    expect(typeof tracer.isAvailable()).toBe('boolean');
  });

  it('should handle tracer appropriately', () => {
    const tracer = system.tracer;

    if (tracer.isAvailable()) {
      // If Perfetto is available, start should not throw
      expect(() => {
        tracer.start({ flow: true });
        tracer.stop();
      }).not.toThrow();
    } else {
      // If not available, should throw when trying to start
      expect(() => {
        tracer.start({ flow: true });
      }).toThrow('Perfetto tracing is not available');
    }
  });

  it('should register symbol names without crashing', () => {
    // Should not throw
    expect(() => {
      system.tracer.registerFunctionNames({
        0x400: 'main',
        0x500: 'helper',
      });
      system.tracer.registerMemoryNames({
        0x100000: 'buffer',
        0x100100: 'data',
      });
    }).not.toThrow();
  });
});
