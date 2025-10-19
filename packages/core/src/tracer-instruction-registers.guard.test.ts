import { jest } from '@jest/globals';
import { createSystem } from './index.js';

const writeLongBE = (buffer: Uint8Array, offset: number, value: number) => {
  buffer[offset] = (value >>> 24) & 0xff;
  buffer[offset + 1] = (value >>> 16) & 0xff;
  buffer[offset + 2] = (value >>> 8) & 0xff;
  buffer[offset + 3] = value & 0xff;
};

const createRom = () => {
  const rom = new Uint8Array(0x1000);
  writeLongBE(rom, 0x0000, 0x00010000); // initial SP
  writeLongBE(rom, 0x0004, 0x00000400); // entry PC

  // minimal program: moveq + stop
  rom.set([0x70, 0x01, 0x4e, 0x72, 0x00, 0x00], 0x0400);
  return rom;
};

describe('Tracer instruction register capability guard', () => {
  it('fails fast before initializing Perfetto when register export is missing', async () => {
    const system = await createSystem({
      rom: createRom(),
      ramSize: 0x2000,
    });

    const tracer: any = system.tracer;
    if (!tracer.isAvailable()) {
      system.cleanup();
      return;
    }

    const musashi: any = tracer._musashi;
    const wasmModule = musashi?._module ?? {};
    const originalExport =
      wasmModule._m68k_perfetto_enable_instruction_registers;

    // Simulate a build missing the register export
    wasmModule._m68k_perfetto_enable_instruction_registers = undefined;

    const initSpy = jest.spyOn(musashi, 'perfettoInit');
    const traceEnableSpy = jest.spyOn(musashi, 'traceEnable');

    try {
      expect(() =>
        tracer.start({ instructions: true, instructionsRegisters: true })
      ).toThrow(/instruction register tracing is not available/i);

      expect(initSpy).not.toHaveBeenCalled();
      expect(traceEnableSpy).not.toHaveBeenCalled();
      expect((tracer as any)._active).toBe(false);
      expect(musashi.perfettoIsInitialized()).toBe(false);
    } finally {
      wasmModule._m68k_perfetto_enable_instruction_registers = originalExport;
      initSpy.mockRestore();
      traceEnableSpy.mockRestore();
      system.cleanup();
    }
  });
});
