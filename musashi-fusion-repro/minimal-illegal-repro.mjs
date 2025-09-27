const NODE_ENTRY = process.env.MUSASHI_NODE ?? 'musashi-wasm/musashi-node.out.mjs';
const CORE_ENTRY = process.env.MUSASHI_CORE ?? 'musashi-wasm/core';

const { default: createLocalMusashi } = await import(NODE_ENTRY);
const { createSystem: createTpSystem, M68kRegister } = await import(CORE_ENTRY);

const ROM_LENGTH = 0x300000;
const RAM_BASE = 0x00100000;
const RAM_SIZE = 0x100000;
const STACK_BASE = 0x0010f300;
const ILLEGAL_PC = 0x00000400;
const WATCH_ADDR = 0x00100a80;

const bytesOf = (size, addr, value) => {
  if (size === 1) return [[addr >>> 0, value & 0xff]];
  if (size === 2) return [[addr >>> 0, (value >> 8) & 0xff], [((addr + 1) >>> 0), value & 0xff]];
  return [
    [addr >>> 0, (value >>> 24) & 0xff],
    [((addr + 1) >>> 0), (value >>> 16) & 0xff],
    [((addr + 2) >>> 0), (value >>> 8) & 0xff],
    [((addr + 3) >>> 0), value & 0xff]
  ];
};

const formatWrites = (label, writes) => {
  console.log(`${label} writes (${writes.length} events):`);
  if (!writes.length) return;
  for (const evt of writes) {
    const bytes = bytesOf(evt.size, evt.addr, evt.value)
      .map(([addr, val]) => `${addr.toString(16)}:${val.toString(16).padStart(2, '0')}`)
      .join(', ');
    console.log(
      `  pc=0x${evt.pc.toString(16)} size=${evt.size} value=0x${evt.value.toString(16)} bytes=[${bytes}]`
    );
  }
};

class LocalRunner {
  constructor(rom) {
    this.rom = rom;
    this.ram = new Uint8Array(RAM_SIZE);
    this.pendingWrites = [];
  }

  async init() {
    this.module = await createLocalMusashi();
    const m = this.module;
    m._m68k_init();
    if (typeof m._my_initialize === 'function') m._my_initialize();
    for (const region of [
      { start: 0x000000, offset: 0, length: 0x100000 },
      { start: 0x200000, offset: 0x100000, length: 0x100000 }
    ]) {
      const slice = this.rom.slice(region.offset, region.offset + region.length);
      const ptr = m._malloc(slice.length);
      if (!ptr) throw new Error('malloc failed');
      m.writeArrayToMemory(slice, ptr);
      m._add_region(region.start, slice.length, ptr);
    }
    const readMem = m.addFunction((addr, size) => this.read(addr >>> 0, size | 0), 'iii');
    const writeMem = m.addFunction(
      (addr, size, value) => this.write(addr >>> 0, size | 0, value >>> 0),
      'viii'
    );
    m._set_read_mem_func(readMem);
    m._set_write_mem_func(writeMem);
  }

  read(addr, size) {
    if (addr >= RAM_BASE && addr < RAM_BASE + RAM_SIZE) {
      const offset = addr - RAM_BASE;
      if (size === 1) return this.ram[offset];
      if (size === 2) return (this.ram[offset] << 8) | this.ram[offset + 1];
      return (
        (this.ram[offset] << 24) |
        (this.ram[offset + 1] << 16) |
        (this.ram[offset + 2] << 8) |
        this.ram[offset + 3]
      ) >>> 0;
    }
    const index = addr >>> 0;
    if (size === 1) return this.rom[index] ?? 0;
    if (size === 2) return ((this.rom[index] ?? 0) << 8) | (this.rom[index + 1] ?? 0);
    return (
      ((this.rom[index] ?? 0) << 24) |
      ((this.rom[index + 1] ?? 0) << 16) |
      ((this.rom[index + 2] ?? 0) << 8) |
      (this.rom[index + 3] ?? 0)
    ) >>> 0;
  }

  write(addr, size, value) {
    if (addr >= RAM_BASE && addr < RAM_BASE + RAM_SIZE) {
      const offset = addr - RAM_BASE;
      if (size === 1) this.ram[offset] = value & 0xff;
      else if (size === 2) {
        this.ram[offset] = (value >> 8) & 0xff;
        this.ram[offset + 1] = value & 0xff;
      } else if (size === 4) {
        this.ram[offset] = (value >>> 24) & 0xff;
        this.ram[offset + 1] = (value >>> 16) & 0xff;
        this.ram[offset + 2] = (value >>> 8) & 0xff;
        this.ram[offset + 3] = value & 0xff;
      }
    }
    const pc = this.getReg(M68kRegister.PC) >>> 0;
    this.pendingWrites.push({ addr, size, value, pc });
  }

  reset() {
    this.module._m68k_pulse_reset();
    this.module._m68k_set_reg(M68kRegister.A0, WATCH_ADDR);
    this.module._m68k_set_reg(M68kRegister.A1, WATCH_ADDR);
  }

  getReg(index) {
    return this.module._m68k_get_reg(0, index) >>> 0;
  }

  step() {
    this.pendingWrites = [];
    const info = this.module._m68k_step_one();
    return {
      writes: this.pendingWrites.slice(),
      cycles: Number(info) >>> 0,
      pc: this.getReg(M68kRegister.PC) >>> 0
    };
  }
}

class TpRunner {
  constructor(rom) {
    this.rom = rom;
    this.pendingWrites = [];
  }

  async init() {
    const memoryLayout = {
      regions: [
        { start: 0x000000, length: 0x100000, source: 'rom', sourceOffset: 0x000000 },
        { start: RAM_BASE, length: RAM_SIZE, source: 'ram' },
        { start: 0x200000, length: 0x100000, source: 'rom', sourceOffset: 0x100000 }
      ],
      minimumCapacity: ROM_LENGTH
    };
    this.system = await createTpSystem({ rom: this.rom, ramSize: RAM_SIZE, memoryLayout });
    this.unsubscribe = this.system.onMemoryWrite((evt) => {
      this.pendingWrites.push({
        addr: evt.addr >>> 0,
        size: evt.size,
        value: evt.value >>> 0,
        pc: evt.pc >>> 0
      });
    });
  }

  reset() {
    this.system.reset();
    this.system.setRegister('a0', WATCH_ADDR);
    this.system.setRegister('a1', WATCH_ADDR);
  }

  step() {
    this.pendingWrites = [];
    const info = this.system.step();
    return {
      writes: this.pendingWrites.slice(),
      cycles: Number(info.cycles) >>> 0,
      pc: info.endPc >>> 0
    };
  }
}

const writeLongBE = (buf, addr, value) => {
  buf[addr + 0] = (value >>> 24) & 0xff;
  buf[addr + 1] = (value >>> 16) & 0xff;
  buf[addr + 2] = (value >>> 8) & 0xff;
  buf[addr + 3] = value & 0xff;
};

(async () => {
  const rom = new Uint8Array(ROM_LENGTH);
  writeLongBE(rom, 0x000000, STACK_BASE);
  writeLongBE(rom, 0x000004, ILLEGAL_PC);
  rom[ILLEGAL_PC] = 0xff;
  rom[ILLEGAL_PC + 1] = 0xff;

  const local = new LocalRunner(rom);
  await local.init();
  local.reset();

  const tp = new TpRunner(new Uint8Array(rom));
  await tp.init();
  tp.reset();

  const localStep = local.step();
  const tpStep = tp.step();

  formatWrites('Local', localStep.writes);
  formatWrites('TP   ', tpStep.writes);
})();
