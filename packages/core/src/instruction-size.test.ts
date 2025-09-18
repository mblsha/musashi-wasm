import { createSystem } from './index';

describe('disassemble().size', () => {
  let sys: any;

  afterEach(() => {
    if (sys && typeof sys.cleanup === 'function') sys.cleanup();
    sys = undefined;
  });

  it('returns correct sizes for common instructions', async () => {
    const rom = new Uint8Array(0x4000);
    sys = await createSystem({ rom, ramSize: 0x20000 });

    // NOP at 0x0600 (0x4E71) -> 2 bytes
    sys.write(0x600, 1, 0x4e);
    sys.write(0x601, 1, 0x71);
    expect(sys.disassemble(0x600)?.size).toBe(2);

    // RTS at 0x0602 (0x4E75) -> 2 bytes
    sys.write(0x602, 1, 0x4e);
    sys.write(0x603, 1, 0x75);
    expect(sys.disassemble(0x602)?.size).toBe(2);

    // MOVEQ #$7f, D3 at 0x0800 (0x73 0x7f) -> 2 bytes
    sys.write(0x800, 1, 0x73);
    sys.write(0x801, 1, 0x7f);
    expect(sys.disassemble(0x800)?.size).toBe(2);

    // LINK A6,#-$8 at 0x0810 (0x4e 0x56 0xff 0xf8) -> 4 bytes
    sys.write(0x810, 1, 0x4e);
    sys.write(0x811, 1, 0x56);
    sys.write(0x812, 1, 0xff);
    sys.write(0x813, 1, 0xf8);
    expect(sys.disassemble(0x810)?.size).toBe(4);

    // ADDI.L #$1, D1 at 0x0860 -> 6 bytes
    const addi = [0x06, 0x81, 0x00, 0x00, 0x00, 0x01];
    for (let i = 0; i < addi.length; i++) sys.write(0x860 + i, 1, addi[i]);
    expect(sys.disassemble(0x860)?.size).toBe(6);

    // JSR $00000600 at 0x0900 (absolute long) -> 6 bytes
    const jsrAbsLong = [0x4e, 0xb9, 0x00, 0x00, 0x06, 0x00];
    for (let i = 0; i < jsrAbsLong.length; i++) sys.write(0x900 + i, 1, jsrAbsLong[i]);
    expect(sys.disassemble(0x900)?.size).toBe(6);

    // DBRA D0,$87e at 0x0A00 -> 4 bytes
    const dbra = [0x51, 0xc8, 0xff, 0xfe];
    for (let i = 0; i < dbra.length; i++) sys.write(0xa00 + i, 1, dbra[i]);
    expect(sys.disassemble(0xa00)?.size).toBe(4);
  });
});
