import { createSystem } from './index.js';
import type { MemoryLayout, System } from './types.js';

function makeRom(size: number): Uint8Array {
  const rom = new Uint8Array(size);
  for (let i = 0; i < size; i++) rom[i] = i & 0xff;
  // Ensure reset vectors exist; wrapper overwrites, but keep sane ROM
  rom[0] = 0x00; rom[1] = 0x10; rom[2] = 0x80; rom[3] = 0x00; // SSP
  rom[4] = 0x00; rom[5] = 0x00; rom[6] = 0x04; rom[7] = 0x00; // PC
  return rom;
}

describe('Unified memory layout (regions + mirrors)', () => {
  let system: System;

  afterEach(() => {
    // Clean up system resources to prevent Jest from hanging
    if (system) {
      system.cleanup();
    }
  });

  it('maps regions, applies mirrors, ensures capacity, and reflects RAM writes', async () => {
    const rom = makeRom(0x300000);
    const ramSize = 0x12000; // must cover sourceOffset + length for RAM region

    const layout: MemoryLayout = {
      regions: [
        // Map 0x1000 bytes of ROM at 0x080000 from ROM offset 0x10000
        { start: 0x080000, length: 0x1000, source: 'rom', sourceOffset: 0x10000 },
        // Map 0x8000 bytes of ROM at 0x200000 from ROM offset 0x18000
        { start: 0x200000, length: 0x8000, source: 'rom', sourceOffset: 0x18000 },
        // Map 0x10000 bytes of RAM at 0x300000 from RAM offset 0x2000 (fits within 0x12000 RAM)
        { start: 0x300000, length: 0x10000, source: 'ram', sourceOffset: 0x2000 },
      ],
      mirrors: [
        // Mirror the 0x8000 window from 0x200000 to 0x210000
        { start: 0x210000, length: 0x8000, mirrorFrom: 0x200000 },
      ],
      minimumCapacity: 0x400000,
    };

    system = await createSystem({ rom, ramSize, memoryLayout: layout });

    // Region 1 checks
    const r1Start = 0x080000;
    const r1Off = 0x10000;
    expect(system.read(r1Start, 1) & 0xff).toBe(rom[r1Off]);
    expect(system.read(r1Start + 0x0fff, 1) & 0xff).toBe(rom[r1Off + 0x0fff]);

    // Region 2 checks
    const r2Start = 0x200000;
    const r2Off = 0x18000;
    expect(system.read(r2Start, 1) & 0xff).toBe(rom[r2Off]);
    expect(system.read(r2Start + 0x7fff, 1) & 0xff).toBe(rom[r2Off + 0x7fff]);

    // Mirror checks: 0x210000 mirrors 0x200000
    const mirStart = 0x210000;
    expect(system.read(mirStart, 1) & 0xff).toBe(rom[r2Off]);
    expect(system.read(mirStart + 0x1234, 1) & 0xff).toBe(rom[r2Off + 0x1234]);
    // Capacity: highest covered address is within readable range
    expect(system.read(mirStart + 0x7fff, 1) & 0xff).toBe(rom[r2Off + 0x7fff]);

    // RAM reflection: region at 0x300000 from RAM offset 0x2000
    const ramStart = 0x300000;
    const ramOff = 0x2000;
    // Initially zeroed
    expect(system.read(ramStart, 1)).toBe(0);
    // Byte write
    system.write(ramStart + 0x10, 1, 0xab);
    expect(system.read(ramStart + 0x10, 1) & 0xff).toBe(0xab);
    expect(system.readBytes(ramStart + 0x10, 1)[0]).toBe(0xab);
    expect((system as any).ram[ramOff + 0x10]).toBe(0xab);
    // 32-bit write (big-endian)
    system.write(ramStart + 0x20, 4, 0x11223344);
    expect((system as any).ram[ramOff + 0x20]).toBe(0x11);
    expect((system as any).ram[ramOff + 0x21]).toBe(0x22);
    expect((system as any).ram[ramOff + 0x22]).toBe(0x33);
    expect((system as any).ram[ramOff + 0x23]).toBe(0x44);
  });

  it('preserves backward-compatible defaults when memoryLayout is omitted', async () => {
    const rom = makeRom(0x200000);
    system = await createSystem({ rom, ramSize: 0x2000 });

    // ROM at 0x000000, RAM at 0x100000 by default
    expect(system.read(0x000004, 1) & 0xff).toBe(rom[4]);
    // Writes to 0x100000 reflect into RAM at offset 0
    system.write(0x100000, 1, 0x5a);
    expect((system as any).ram[0]).toBe(0x5a);
  });
});

