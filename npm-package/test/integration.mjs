import assert from 'node:assert/strict';

const pkgRoot = new URL('..', import.meta.url);

const indexModuleUrl = new URL('lib/index.mjs', pkgRoot);
const coreModule = await import(indexModuleUrl);

assert.equal(typeof coreModule.default, 'function', 'default Musashi export must be a class/function');
assert.equal(typeof coreModule.createSystem, 'function', 'createSystem export must be present');
assert.equal(typeof coreModule.M68kRegister, 'object', 'M68kRegister enum should be exported');
assert.equal(coreModule.M68kRegister.PC, 16, 'M68kRegister.PC should equal 16');

const Musashi = coreModule.default;
const musashiInstance = new Musashi();
await musashiInstance.init();
const ptr = musashiInstance.allocateMemory(256);
musashiInstance.writeMemory(ptr, new Uint8Array(8));
musashiInstance.freeMemory(ptr);
assert.equal(typeof musashiInstance.setReadMemFunc, 'function', 'Musashi should expose setReadMemFunc');
assert.equal(typeof musashiInstance.setWriteMemFunc, 'function', 'Musashi should expose setWriteMemFunc');

const rom = new Uint8Array(0x2000);
// Seed reset vector with a NOP at 0x0400 so single-step has real code.
rom[0x400] = 0x4e;
rom[0x401] = 0x71;
const system = await coreModule.createSystem({ rom, ramSize: 0x2000 });

system.reset();

const registers = system.getRegisters();
assert.equal(typeof registers.pc, 'number', 'system.getRegisters() should expose numeric pc');

const stepResult = system.step();
assert.equal(typeof stepResult.cycles, 'number', 'step() must return cycle count');
assert.equal(typeof stepResult.endPc, 'number', 'step() must expose updated PC');

system.cleanup();

// Regression guard: ensure ANDI byte immediate sets Z when zeroing a register.
const ENTRY_PC = 0x0200;
const romWithAndi = new Uint8Array(0x1000);
const writeLong = (mem, offset, value) => {
  mem[offset + 0] = (value >>> 24) & 0xff;
  mem[offset + 1] = (value >>> 16) & 0xff;
  mem[offset + 2] = (value >>> 8) & 0xff;
  mem[offset + 3] = value & 0xff;
};

const readWord = (mem, offset) => ((mem[offset] << 8) | mem[offset + 1]) >>> 0;

const INITIAL_SP = 0x00001000;

writeLong(romWithAndi, 0x0000, INITIAL_SP); // initial SP inside RAM window
writeLong(romWithAndi, 0x0004, ENTRY_PC); // initial PC

const andiProgram = [
  0x02, 0x00, 0x00, 0x02, // andi.b  #$2, D0 -> 0x20 & 0x02 === 0
  0x4e, 0x72, 0x27, 0x00, // stop    #$2700
];

andiProgram.forEach((byte, index) => {
  romWithAndi[ENTRY_PC + index] = byte;
});

assert.equal(readWord(romWithAndi, ENTRY_PC), 0x0200, 'entry word must be ANDI opcode');
assert.equal(readWord(romWithAndi, ENTRY_PC + 2), 0x0002, 'ANDI immediate must equal 0x0002');

const andiSystem = await coreModule.createSystem({ rom: romWithAndi, ramSize: 0x20000 });
andiSystem.reset();
andiSystem.setRegister('d0', 0x20);

const stepOne = andiSystem.step();

assert.equal(stepOne.startPc >>> 0, ENTRY_PC, 'step() must begin at reset PC');
assert.equal(stepOne.endPc >>> 0, ENTRY_PC + 4, 'ANDI instruction size should be 4 bytes');

const afterAndiRegisters = andiSystem.getRegisters();
const srAfterAndi = afterAndiRegisters.sr >>> 0;
const zFlagSet = (srAfterAndi & 0x04) !== 0;

assert.equal(afterAndiRegisters.d0 >>> 0, 0, 'ANDI must clear D0 when masking to zero');
assert.equal(afterAndiRegisters.pc >>> 0, ENTRY_PC + 4, 'PC must advance past ANDI');
assert.equal(zFlagSet, true, 'andi.b zero-result must set the Z flag');

const stepTwo = andiSystem.step();
assert.equal(stepTwo.startPc >>> 0, ENTRY_PC + 4, 'second step should begin at STOP opcode');

const afterStopRegisters = andiSystem.getRegisters();
const srAfterStop = afterStopRegisters.sr >>> 0;

assert.equal(srAfterStop, 0x2700, 'STOP #$2700 must load SR with provided immediate');
assert.equal(afterStopRegisters.pc >>> 0, ENTRY_PC + 8, 'STOP must advance PC past its operand');

andiSystem.cleanup();

musashiInstance.pulseReset?.();

const coreEntry = await import(new URL('lib/core/index.js', pkgRoot));
assert.equal(typeof coreEntry.createSystem, 'function', 'lib/core should export createSystem');
assert.equal(coreEntry.M68kRegister.PC, 16, 'lib/core should expose M68kRegister enum');

const memoryModuleUrl = new URL('lib/memory/index.js', pkgRoot);
const memoryModule = await import(memoryModuleUrl);

assert.equal(typeof memoryModule.MemoryRegion, 'function', 'MemoryRegion export must be present');
assert.equal(typeof memoryModule.MemoryArray, 'function', 'MemoryArray export must be present');
assert.equal(typeof memoryModule.DataParser, 'function', 'DataParser export must be present');

const backing = new Uint8Array(16);
const stubSystem = {
  readBytes(addr, len) {
    return backing.slice(addr, addr + len);
  },
  writeBytes(addr, data) {
    backing.set(data, addr);
  },
};

const region = new memoryModule.MemoryRegion(
  stubSystem,
  0,
  4,
  (bytes) => memoryModule.DataParser.readUint32BE(bytes)
);

memoryModule.DataParser.writeUint32BE(backing, 0x12345678, 0);
assert.equal(region.get(), 0x12345678, 'MemoryRegion should read back written data');

const array = new memoryModule.MemoryArray(
  stubSystem,
  0,
  2,
  (bytes) => memoryModule.DataParser.readUint16BE(bytes)
);

const elementBytes = new Uint8Array(2);
memoryModule.DataParser.writeUint16BE(elementBytes, 0xABCD, 0);
array.setAt(1, elementBytes);

assert.equal(array.at(1), 0xABCD, 'MemoryArray should read values written with setAt');

// Also ensure iterator exposure works for at least one element.
const iterValues = Array.from(array.iterate(1, 1));
assert.deepEqual(iterValues, [0xABCD], 'iterate should yield the written element');

const perfLoaderUrl = new URL('dist/musashi-perfetto-loader.mjs', pkgRoot);
const perfExists = await import('node:fs/promises')
  .then(fs => fs
    .access(perfLoaderUrl)
    .then(() => true)
    .catch(() => false));

if (perfExists) {
  const perfEntrypoint = await import(new URL('perf.mjs', pkgRoot));
  assert.equal(typeof perfEntrypoint.default, 'function', 'perf entry should export default loader');

  const perfWrapper = await import(new URL('lib/perfetto.mjs', pkgRoot));
  assert.equal(typeof perfWrapper.default, 'function', 'perfetto wrapper should export default class');
} else {
  console.warn('⚠️  Perfetto artifacts not present; skipping perf exports smoke checks');
}

const nodeFactory = await import(new URL('musashi-node.out.mjs', pkgRoot));
assert.equal(typeof nodeFactory.default, 'function', 'musashi-node.out.mjs should export default factory');

// Run the TP/core vs WASM fusion regression guard. This script throws on divergence.
await import(new URL('./fusion-divergence.test.mjs', import.meta.url));

console.log('✅ musashi-wasm integration smoke test passed');
