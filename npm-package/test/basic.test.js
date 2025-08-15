const Musashi = require('../lib/index.js');
const { MusashiPerfetto } = require('../lib/perfetto.js');

describe('Musashi WASM Package', () => {
  describe('Standard Build', () => {
    let cpu;
    let memPtr;
    const memSize = 1024 * 1024;

    beforeEach(async () => {
      cpu = new Musashi();
      await cpu.init();
      memPtr = cpu.allocateMemory(memSize);
    });

    afterEach(() => {
      if (cpu && memPtr) {
        cpu.clearRegions();
        cpu.freeMemory(memPtr);
      }
    });

    test('should initialize successfully', () => {
      expect(cpu).toBeDefined();
      expect(cpu.initialized).toBe(true);
    });

    test('should execute reset sequence', () => {
      const memory = new Uint8Array(memSize);
      
      // Set up reset vectors
      const resetSP = 0x00001000;
      const resetPC = 0x00000400;
      
      // Write vectors (big-endian)
      memory[0] = (resetSP >> 24) & 0xFF;
      memory[1] = (resetSP >> 16) & 0xFF;
      memory[2] = (resetSP >> 8) & 0xFF;
      memory[3] = resetSP & 0xFF;
      memory[4] = (resetPC >> 24) & 0xFF;
      memory[5] = (resetPC >> 16) & 0xFF;
      memory[6] = (resetPC >> 8) & 0xFF;
      memory[7] = resetPC & 0xFF;
      
      // Write NOP at reset PC
      memory[resetPC] = 0x4E;
      memory[resetPC + 1] = 0x71;
      
      // Set up memory callbacks
      cpu.setReadMemFunc((address) => {
        return memory[address & (memSize - 1)];
      });
      
      cpu.setWriteMemFunc((address, value) => {
        memory[address & (memSize - 1)] = value;
      });
      
      // Copy to WASM heap and map
      cpu.writeMemory(memPtr, memory);
      cpu.addRegion(0, memSize, memPtr);
      
      // Reset and check PC
      cpu.pulseReset();
      const pc = cpu.getReg(16); // PC register
      expect(pc).toBe(resetPC);
    });

    test('should execute instructions', () => {
      const memory = new Uint8Array(memSize);
      
      // Setup as before
      const resetSP = 0x00001000;
      const resetPC = 0x00000400;
      
      memory[0] = (resetSP >> 24) & 0xFF;
      memory[1] = (resetSP >> 16) & 0xFF;
      memory[2] = (resetSP >> 8) & 0xFF;
      memory[3] = resetSP & 0xFF;
      memory[4] = (resetPC >> 24) & 0xFF;
      memory[5] = (resetPC >> 16) & 0xFF;
      memory[6] = (resetPC >> 8) & 0xFF;
      memory[7] = resetPC & 0xFF;
      
      // Write multiple NOPs
      for (let i = 0; i < 10; i++) {
        memory[resetPC + i * 2] = 0x4E;
        memory[resetPC + i * 2 + 1] = 0x71;
      }
      
      cpu.setReadMemFunc((address) => memory[address & (memSize - 1)]);
      cpu.setWriteMemFunc((address, value) => {
        memory[address & (memSize - 1)] = value;
      });
      
      cpu.writeMemory(memPtr, memory);
      cpu.addRegion(0, memSize, memPtr);
      
      cpu.pulseReset();
      const cycles = cpu.execute(100);
      
      expect(cycles).toBeGreaterThan(0);
      expect(cpu.getReg(16)).toBeGreaterThan(resetPC); // PC should advance
    });
  });

  describe('Perfetto Build', () => {
    let cpu;

    beforeEach(async () => {
      cpu = new MusashiPerfetto();
      await cpu.init('TestEmulator');
    });

    test('should initialize with Perfetto support', () => {
      expect(cpu).toBeDefined();
      expect(cpu.initialized).toBe(true);
    });

    test('should enable tracing modes', () => {
      expect(() => {
        cpu.enableFlowTracing(true);
        cpu.enableInstructionTracing(true);
        cpu.enableMemoryTracing(true);
        cpu.enableInterruptTracing(true);
      }).not.toThrow();
    });

    test('should export empty trace', async () => {
      const trace = await cpu.exportTrace();
      // Trace might be null if no events were recorded
      expect(trace === null || trace instanceof Uint8Array).toBe(true);
    });
  });
});