// ESM-compatible test file using real WASM
// @ts-nocheck
import { createSystem, formatFaultContext } from './index.js';
// Shared test utilities
import { BreakReason, getLastBreakReasonFrom, resetLastBreakReasonOn } from './test-utils.js';

describe('@m68k/core', () => {
  let system: any;

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
    // Clean up system resources to prevent Jest from hanging
    system.cleanup();
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

  it('provides raw byte helpers without triggering trace callbacks', () => {
    const ramBase = 0x100000;
    const addr = ramBase + 0x20;
    const writes: unknown[] = [];
    const writesStop = system.onMemoryWrite(evt => {
      writes.push(evt);
    });
    const reads: unknown[] = [];
    const readsStop = system.onMemoryRead(evt => {
      reads.push(evt);
    });

    expect(system.readRaw8(addr)).toBe(0);
    expect(reads).toHaveLength(0);

    system.writeRaw8(addr, 0x5a);
    expect(system.readRaw8(addr)).toBe(0x5a);
    expect(reads).toHaveLength(0);
    expect(writes).toHaveLength(0);

    readsStop();

    expect(system.read(addr, 1)).toBe(0x5a);
    expect(system.readBytes(addr, 1)[0]).toBe(0x5a);

    const outside = 0x400000;
    system.writeRaw8(outside, 0xaa);
    expect(system.readRaw8(outside)).toBe(0);
    expect(writes).toHaveLength(0);

    writesStop();
  });

  it('handles unsigned 32-bit values correctly', () => {
    const ramBase = 0x100000;
    const value = 0xf2345678;

    system.write(ramBase, 4, value);
    const readBack = system.read(ramBase, 4);
    expect(readBack).toBe(value >>> 0);
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
    const cycles = system.run(100);
    const execResult = system.consumeLastExecResult();

    // Should have executed something
    expect(cycles).toBeGreaterThan(0);
    expect(execResult?.cycles).toBe(cycles);
    expect(execResult?.reason).toBe('returned');
    expect(execResult?.fault).toBeUndefined();

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
    system.run(50);

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

    system.run(100);

    expect(calls.length).toBe(1);
    expect(system.getRegisters().d1).toBe(0x42);
    expect(system.getRegisters().pc).toBe(0x40e);

    removeOverride();
  });

  it('removing a stale override does not clear the active override', () => {
    const overrideAddress = 0x408;
    const staleCalls: number[] = [];
    const removeStale = system.override(overrideAddress, sys => {
      staleCalls.push(sys.getRegisters().pc);
    });

    const activeCalls: number[] = [];
    const removeActive = system.override(overrideAddress, sys => {
      activeCalls.push(sys.getRegisters().pc);
      sys.setRegister('pc', 0x40e);
    });

    removeStale();

    system.run(100);

    expect(staleCalls.length).toBe(0);
    expect(activeCalls.length).toBe(1);

    removeActive();
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

    const callCycles = system.call(subAddr);
    const callResult = system.consumeLastExecResult();
    expect(callCycles).toBeGreaterThan(0);
    expect(callResult?.cycles).toBe(callCycles);
    expect(callResult?.reason).toBe('returned');
    expect(callResult?.fault).toBeUndefined();
    expect(system.getRegisters().d2 >>> 0).toBe(0xcafebabe);

    // Assert break reason came from JS hook (override)
    const br = getLastBreakReasonFrom(system);
    expect(br).toBe(BreakReason.JsHook);
    resetLastBreakReasonOn(system);

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

    const callCycles = system.call(0x500);
    const callResult = system.consumeLastExecResult();
    expect(callCycles).toBeGreaterThan(0);
    expect(callResult?.cycles).toBe(callCycles);
    expect(callResult?.reason).toBe('returned');
    expect(callResult?.fault).toBeUndefined();
    // D2 should be DEADBEEF + 1 from ADD.L
    expect(system.getRegisters().d2 >>> 0).toBe(0xdeadbeef + 1 >>> 0);

    // Assert break reason came from JS hook (override)
    const br2 = getLastBreakReasonFrom(system);
    expect(br2).toBe(BreakReason.JsHook);
    resetLastBreakReasonOn(system);

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

  it('should invoke onMemoryRead/Write with PC context', async () => {
    // Small subroutine at 0x600:
    // MOVE.L #$CAFEBABE, D0
    // MOVE.L D0, -(SP)
    // MOVE.L (SP)+, D1
    // RTS
    const subAddr = 0x600;
    const code = new Uint8Array([
      0x20, 0x3c, 0xca, 0xfe, 0xba, 0xbe,
      0x2f, 0x00,
      0x22, 0x1f,
      0x4e, 0x75,
    ]);
    system.writeBytes(subAddr, code);

    // Place SP so that pre-decrement lands at 0x100000
    system.setRegister('sp', 0x100004);

    const writes: Array<{ addr: number; size: number; value: number; pc: number }>
      = [];
    const reads: Array<{ addr: number; size: number; value: number; pc: number }>
      = [];

    const offW = system.onMemoryWrite(({ addr, size, value, pc }) => {
      writes.push({ addr, size, value, pc });
    });
    const offR = system.onMemoryRead(({ addr, size, value, pc }) => {
      reads.push({ addr, size, value, pc });
    });

    const callCycles = system.call(subAddr);
    const callResult = system.consumeLastExecResult();
    expect(callCycles).toBeGreaterThan(0);
    expect(callResult?.cycles).toBe(callCycles);
    expect(callResult?.reason).toBe('returned');
    expect(callResult?.fault).toBeUndefined();

    // Validate write event from MOVE.L D0, -(SP)
    const pushEvents = writes.filter(ev => (ev.pc >>> 0) === (subAddr + 6));
    expect(pushEvents.length).toBeGreaterThan(0);
    const baseAddr = Math.min(...pushEvents.map(ev => ev.addr >>> 0));
    expect(baseAddr).toBe(0x100000);
    const combinedValue = pushEvents.reduce((acc, ev) => {
      const offset = (ev.addr >>> 0) - baseAddr;
      if (ev.size === 4) {
        return ev.value >>> 0;
      }
      const mask = ev.size === 2 ? 0xffff : 0xff;
      const shift = (4 - ev.size - offset) * 8;
      return acc | ((ev.value & mask) << shift);
    }, 0);
    expect(combinedValue >>> 0).toBe(0xcafebabe);

    // Validate read event from MOVE.L (SP)+, D1
    const popEvent = reads.find(ev => (ev.pc >>> 0) === (subAddr + 8));
    expect(popEvent).toBeDefined();
    expect(popEvent!.addr >>> 0).toBe(0x100000);
  expect(popEvent!.value >>> 0).toBe(0xcafebabe);

  offW();
  offR();
  });

  describe('fault reporting', () => {
    it('captures handler callback errors', async () => {
      const subAddr = 0x410;
      const sub = new Uint8Array([
        0x24, 0x3c,
        0xca, 0xfe, 0xba, 0xbe,
        0x4e, 0x75,
      ]);
      system.writeBytes(subAddr, sub);
      const rtsPc = subAddr + 6;
      const boom = new Error('handler boom');
      const removeOverride = system.override(rtsPc, () => {
        throw boom;
      });

      const callCycles = system.call(subAddr);
      expect(callCycles).toBeGreaterThanOrEqual(0);
      const execResult = system.consumeLastExecResult();
      expect(execResult?.reason).toBe('fault');
      const fault = execResult?.fault;
      expect(fault).toBeDefined();
      expect(fault?.kind).toBe('handler_error');
      expect(fault?.cause).toBe(boom);
      expect(formatFaultContext(fault!)).toContain('Callback');

      expect(system.consumeLastFault()).toBe(fault);
      expect(system.consumeLastFault()).toBeUndefined();

      removeOverride();
    });

    it('captures native fault records', async () => {
      const musashi = (system as any)._musashi as any;

      const module = musashi._module as any;
      const ptr = musashi.faultRecordPointer();
      const base = ptr >>> 2;
      const heap = module.HEAPU32;
      const nativeError = new Error('native fault');
      const originalExecute = musashi.execute.bind(musashi);

      musashi.execute = () => {
        heap[base] = 1;
        heap[base + 1] = 2;
        heap[base + 2] = 3;
        heap[base + 3] = 0x00010003;
        heap[base + 4] = 2;
        heap[base + 5] = 0x00000500;
        heap[base + 6] = 0x000004fe;
        heap[base + 7] = 0x00002000;
        heap[base + 8] = 0x00002700;
        heap[base + 9] = 0x00004afc;
        heap[base + 10] = 1;
        throw nativeError;
      };

      try {
        const cycles = system.run(10);
        expect(cycles).toBeGreaterThanOrEqual(0);
        const execResult = system.consumeLastExecResult();
        expect(execResult?.reason).toBe('fault');
        const fault = execResult?.fault;
        expect(fault).toBeDefined();
        expect(fault?.kind).toBe('address_error');
        expect(fault?.write).toBe(true);
        expect(fault?.vector).toBe(3);
        expect(fault?.address).toBe(0x00010003);
        expect(fault?.cause).toBe(nativeError);
        expect(formatFaultContext(fault!)).toContain('Address error');
        expect(system.consumeLastFault()).toBe(fault);
        expect(system.consumeLastFault()).toBeUndefined();
      } finally {
        musashi.execute = originalExecute;
        module._m68k_fault_clear?.();
      }
    });

    it('formats illegal instruction faults', () => {
      const fault = {
        kind: 'illegal_instruction',
        pc: 0x400,
        ppc: 0x3fc,
        sp: 0x1000,
        sr: 0x2700,
        opcode: 0x4afc,
      };
      expect(formatFaultContext(fault)).toContain('Illegal instruction');
    });
  });
});
