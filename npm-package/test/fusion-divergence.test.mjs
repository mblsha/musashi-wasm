import assert from 'node:assert/strict';
import createLocalMusashi from '../musashi-node.out.mjs';
import { createSystem as createTpSystem, M68kRegister } from '../lib/core/index.js';

const CALL_ENTRY = 0x00000400;
const MOVEM_PC = CALL_ENTRY;
const JSR1_PC = CALL_ENTRY + 4;
const RETURN_PC = CALL_ENTRY + 10;
const TARGET_A = 0x0005dc1c;
const STACK_BASE = 0x0010f300;
const RAM_BASE = 0x00100000;
const ROM_LENGTH = 0x00300000;
const A0_PROBE = 0x00100a80;
const STEP_LIMIT = 200_000;

const mask24 = (value) => value & 0x00ffffff;

const writeBytes = (buf, addr, bytes) => {
  for (let i = 0; i < bytes.length; i += 1) {
    buf[addr + i] = bytes[i] & 0xff;
  }
};

const writeLongBE = (buf, addr, value) => {
  buf[addr + 0] = (value >>> 24) & 0xff;
  buf[addr + 1] = (value >>> 16) & 0xff;
  buf[addr + 2] = (value >>> 8) & 0xff;
  buf[addr + 3] = value & 0xff;
};

const emitBytes = (size, addr, value) => {
  const start = addr >>> 0;
  const number = value >>> 0;
  if (size === 1) return [[start, number & 0xff]];
  if (size === 2) {
    return [
      [start + 0, (number >> 8) & 0xff],
      [start + 1, number & 0xff],
    ];
  }
  return [
    [start + 0, (number >>> 24) & 0xff],
    [start + 1, (number >>> 16) & 0xff],
    [start + 2, (number >>> 8) & 0xff],
    [start + 3, number & 0xff],
  ];
};

class LocalRunner {
  constructor(rom) {
    this.rom = rom;
    this.ram = new Uint8Array(0x100000);
    this.pendingWrites = [];
  }

  async init() {
    this.module = await createLocalMusashi();
    const m = this.module;
    m._m68k_init();
    if (typeof m._my_initialize === 'function') {
      m._my_initialize();
    }

    const regions = [
      { start: 0x000000, length: 0x100000, offset: 0x000000 },
      { start: 0x200000, length: 0x100000, offset: 0x100000 },
    ];
    for (const region of regions) {
      const slice = this.rom.slice(region.offset, region.offset + region.length);
      const ptr = m._malloc(region.length);
      if (!ptr) throw new Error('malloc failed for ROM region');
      m.writeArrayToMemory(slice, ptr);
      m._add_region(region.start, region.length, ptr);
    }

    const readMem = m.addFunction((addr, size) => this.readMemory(addr >>> 0, size | 0), 'iii');
    const writeMem = m.addFunction((addr, size, value) => this.writeMemory(addr >>> 0, size | 0, value >>> 0), 'viii');
    m._set_read_mem_func(readMem);
    m._set_write_mem_func(writeMem);
  }

  readMemory(addr, size) {
    if (addr >= RAM_BASE && addr < RAM_BASE + this.ram.length) {
      const offset = addr - RAM_BASE;
      if (size === 1) return this.ram[offset];
      if (size === 2) return (this.ram[offset] << 8) | this.ram[offset + 1];
      return (
        (this.ram[offset] << 24)
        | (this.ram[offset + 1] << 16)
        | (this.ram[offset + 2] << 8)
        | this.ram[offset + 3]
      ) >>> 0;
    }
    const base = addr >= 0x200000 ? 0x100000 : 0x000000;
    const index = addr - base;
    if (size === 1) return this.rom[index] ?? 0;
    if (size === 2) return ((this.rom[index] ?? 0) << 8) | (this.rom[index + 1] ?? 0);
    return (
      ((this.rom[index] ?? 0) << 24)
      | ((this.rom[index + 1] ?? 0) << 16)
      | ((this.rom[index + 2] ?? 0) << 8)
      | (this.rom[index + 3] ?? 0)
    ) >>> 0;
  }

  writeMemory(addr, size, value) {
    if (addr >= RAM_BASE && addr < RAM_BASE + this.ram.length) {
      const offset = addr - RAM_BASE;
      if (size === 1) {
        this.ram[offset] = value & 0xff;
      } else if (size === 2) {
        this.ram[offset] = (value >> 8) & 0xff;
        this.ram[offset + 1] = value & 0xff;
      } else if (size === 4) {
        this.ram[offset] = (value >>> 24) & 0xff;
        this.ram[offset + 1] = (value >>> 16) & 0xff;
        this.ram[offset + 2] = (value >>> 8) & 0xff;
        this.ram[offset + 3] = value & 0xff;
      }
    }
    this.pendingWrites.push({
      addr: addr >>> 0,
      size,
      value: value >>> 0,
      pc: mask24(this.getReg(M68kRegister.PC) >>> 0),
      ppc: mask24(this.module._m68k_get_reg(0, M68kRegister.PPC) >>> 0),
      bytes: emitBytes(size, addr, value),
      source: 'local-shim',
    });
  }

  reset() {
    this.module._m68k_pulse_reset();
  }

  setReg(index, value) {
    this.module._m68k_set_reg(index, value >>> 0);
  }

  getReg(index) {
    return this.module._m68k_get_reg(0, index) >>> 0;
  }

  step() {
    this.pendingWrites = [];
    const startPc = this.getReg(M68kRegister.PC);
    const cycles = this.module._m68k_step_one();
    const endPc = this.getReg(M68kRegister.PC);
    return {
      startPc,
      endPc,
      cycles: Number(cycles) >>> 0,
      writes: this.pendingWrites.slice(),
    };
  }
}

class TpRunner {
  constructor(rom) {
    this.rom = rom;
    this.pendingWrites = [];
    this.sources = new Set();
  }

  async init() {
    const memoryLayout = {
      regions: [
        { start: 0x000000, length: 0x100000, source: 'rom', sourceOffset: 0x000000 },
        { start: 0x100000, length: 0x100000, source: 'ram' },
        { start: 0x200000, length: 0x100000, source: 'rom', sourceOffset: 0x100000 },
      ],
      minimumCapacity: ROM_LENGTH,
    };
    this.system = await createTpSystem({
      rom: this.rom,
      ramSize: 0x100000,
      memoryLayout,
    });
    this.unsubscribe = this.system.onMemoryWrite((evt) => {
      this.sources.add(evt.source ?? 'unknown');
      const ppcValue = evt.ppc === undefined ? undefined : mask24(evt.ppc >>> 0);
      this.pendingWrites.push({
        addr: evt.addr >>> 0,
        size: evt.size >>> 0,
        value: evt.value >>> 0,
        pc: mask24(evt.pc >>> 0),
        ppc: ppcValue,
        bytes: emitBytes(evt.size, evt.addr >>> 0, evt.value >>> 0),
        source: evt.source,
      });
    });
  }

  reset() {
    this.system.reset();
  }

  setReg(name, value) {
    this.system.setRegister(name, value >>> 0);
  }

  step() {
    this.pendingWrites = [];
    const info = this.system.step();
    return {
      startPc: info.startPc >>> 0,
      endPc: info.endPc >>> 0,
      cycles: Number(info.cycles) >>> 0,
      writes: this.pendingWrites.slice(),
    };
  }
}

const buildRom = () => {
  const rom = new Uint8Array(ROM_LENGTH);
  writeLongBE(rom, 0x000000, STACK_BASE);
  writeLongBE(rom, 0x000004, CALL_ENTRY);
  writeBytes(rom, MOVEM_PC, [0x48, 0xe7, 0xff, 0xfe]);
  writeBytes(rom, JSR1_PC, [0x4e, 0xb9, 0x00, 0x05, 0xdc, 0x1c]);
  writeBytes(rom, RETURN_PC, [0x4e, 0x75]);
  writeBytes(rom, TARGET_A + 0, [0x30, 0x3c, 0x00, 0x9c]);
  writeBytes(rom, TARGET_A + 4, [0x21, 0xbc, 0xff, 0xff, 0xff, 0xff]);
  writeBytes(rom, TARGET_A + 10, [0x4e, 0x75]);
  return rom;
};

const rom = buildRom();
const local = new LocalRunner(rom);
const tp = new TpRunner(new Uint8Array(rom));
await local.init();
await tp.init();

local.reset();
tp.reset();

const setLocal = (reg, value) => local.setReg(reg, value >>> 0);
setLocal(M68kRegister.A7, STACK_BASE);
setLocal(M68kRegister.SP, STACK_BASE);
setLocal(M68kRegister.A0, A0_PROBE);
setLocal(M68kRegister.A1, A0_PROBE);
setLocal(M68kRegister.D0, 0x0000009c);
setLocal(M68kRegister.D1, 0);
setLocal(M68kRegister.SR, 0x2704);
setLocal(M68kRegister.PC, CALL_ENTRY);

tp.setReg('sp', STACK_BASE);
tp.setReg('pc', CALL_ENTRY);
tp.setReg('a0', A0_PROBE);
tp.setReg('a1', A0_PROBE);
tp.setReg('d0', 0x0000009c);
tp.setReg('d1', 0);
tp.setReg('sr', 0x2704);

let tpA0Event = null;
let localA0Event = null;

for (let step = 0; step < STEP_LIMIT; step += 1) {
  const localStep = local.step();
  const tpStep = tp.step();

  const localEvent = localStep.writes.find((evt) => evt.bytes.some(([addr]) => mask24(addr) === A0_PROBE));
  if (localEvent) {
    localA0Event = { step, event: localEvent };
    break;
  }

  const tpEvent = tpStep.writes.find((evt) => evt.bytes.some(([addr]) => mask24(addr) === A0_PROBE));
  if (tpEvent) {
    tpA0Event = { step, event: tpEvent };
    break;
  }

  const nextPc = mask24(tpStep.endPc);
  if (nextPc < 0x0400) {
    break;
  }
}

assert.equal(localA0Event, null, localA0Event ? JSON.stringify(localA0Event, null, 2) : '');
assert.equal(tpA0Event, null, tpA0Event ? JSON.stringify(tpA0Event, null, 2) : '');

const seenSources = [...tp.sources];
assert.ok(seenSources.length > 0, 'expected musashi trace events from core');
const unexpectedSources = seenSources.filter((source) => source !== 'core-trace');
assert.equal(
  unexpectedSources.length,
  0,
  `unexpected memory event sources: ${JSON.stringify(seenSources)}`,
);

console.log('✅ TP/core emits no unexpected (A0) write for fusion divergence repro');
