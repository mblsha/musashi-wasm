import createLocalMusashi from 'musashi-wasm/musashi-node.out.mjs';
import { createSystem as createTpSystem, M68kRegister } from 'musashi-wasm/core';

const CALL_ENTRY = 0x0400;
const MOVEM_PC = CALL_ENTRY;
const JSR1_PC = CALL_ENTRY + 4;
const RETURN_PC = CALL_ENTRY + 10;
const TARGET_A = 0x5dc1c;
const STACK_BASE = 0x0010f300;
const RAM_BASE = 0x00100000;
const ROM_LENGTH = 0x300000;
const ENABLE_VECTOR_SENTINELS = process.env.MUSASHI_SENTINELS !== '0';

const bytesOf = (size, addr, value) => {
  const a = addr >>> 0;
  const v = value >>> 0;
  if (size === 1) return [[a, v & 0xff]];
  if (size === 2) return [[a, (v >> 8) & 0xff], [a + 1, v & 0xff]];
  return [
    [a, (v >>> 24) & 0xff],
    [a + 1, (v >>> 16) & 0xff],
    [a + 2, (v >>> 8) & 0xff],
    [a + 3, v & 0xff]
  ];
};

const formatCcFlags = (sr) => {
  const flags = [];
  if (sr & 0x10) flags.push('X');
  if (sr & 0x08) flags.push('N');
  if (sr & 0x04) flags.push('Z');
  if (sr & 0x02) flags.push('V');
  if (sr & 0x01) flags.push('C');
  return flags.join('');
};

const classifyAddr = (addr) => {
  if (addr >= STACK_BASE - 0x100 && addr < STACK_BASE + 0x1000) return 'stack';
  if (addr >= RAM_BASE && addr < RAM_BASE + 0x100000) return 'ram';
  if (addr < 0x100000) return 'rom0';
  if (addr >= 0x200000 && addr < 0x300000) return 'rom1';
  return 'misc';
};

const toByteMap = (writes) => {
  const map = new Map();
  for (const evt of writes) {
    for (const [addr, value] of evt.bytes) {
      map.set(addr >>> 0, value & 0xff);
    }
  }
  return map;
};

const formatBytes = (map) =>
  Array.from(map.entries())
    .sort((a, b) => a[0] - b[0])
    .map(([addr, value]) => `${addr.toString(16)}:${value.toString(16).padStart(2, '0')}`)
    .join(', ');

class LocalRunner {
  constructor(rom) {
    this.rom = rom;
    this.ram = new Uint8Array(0x100000);
    this.pendingWrites = [];
    this.sequence = 0;
    this.traceSequence = 0;
    this.traceWrites = [];
    this.useTraceMem = false;
  }
  async init() {
    this.module = await createLocalMusashi();
    const m = this.module;
    m._m68k_init();
    if (typeof m._my_initialize === 'function') m._my_initialize();
    const regions = [
      { start: 0x000000, length: 0x100000, offset: 0x000000 },
      { start: 0x200000, length: 0x100000, offset: 0x100000 }
    ];
    for (const region of regions) {
      const slice = this.rom.slice(region.offset, region.offset + region.length);
      const ptr = m._malloc(region.length);
      if (!ptr) throw new Error('malloc failed for ROM region');
      m.writeArrayToMemory(slice, ptr);
      m._add_region(region.start, region.length, ptr);
    }
    const readMem = m.addFunction((addr, size) => this.readMemory(addr >>> 0, size | 0), 'iii');
    m._set_read_mem_func(readMem);
    const writeMem = m.addFunction(
      (addr, size, value) => this.writeMemory(addr >>> 0, size | 0, value >>> 0),
      'viii'
    );
    m._set_write_mem_func(writeMem);

    if (
      typeof m._m68k_set_trace_mem_callback === 'function' &&
      typeof m._m68k_trace_enable === 'function' &&
      typeof m._m68k_trace_set_mem_enabled === 'function'
    ) {
      const makeCb = () =>
        (type, pc, addr, value, size) => {
          const eventType = type | 0;
          if (eventType !== 1) return 0;
          const sz = (size | 0) >>> 0;
          if (sz !== 1 && sz !== 2 && sz !== 4) return 0;
          const seq = ++this.traceSequence;
          const addr32 = addr >>> 0;
          const val32 = value >>> 0;
          const pcRaw = pc >>> 0;
          const sr = this.getReg(M68kRegister.SR) & 0xffff;
          this.traceWrites.push({
            addr: addr32,
            size: sz,
            value: val32,
            bytes: bytesOf(sz, addr32, val32),
            pc: pcRaw & 0xffffff,
            pcRaw,
            sr,
            srFlags: formatCcFlags(sr),
            sequence: seq,
            region: classifyAddr(addr32)
          });
          return 0;
        };
      let cbPtr = 0;
      try {
        cbPtr = m.addFunction(makeCb(), 'iiiiiij');
      } catch {
        cbPtr = m.addFunction(makeCb(), 'iiiiiii');
      }
      m._m68k_set_trace_mem_callback(cbPtr);
      m._m68k_trace_enable(1);
      m._m68k_trace_set_mem_enabled(1);
      this.useTraceMem = true;
    }
  }
  readMemory(addr, size) {
    if (addr >= RAM_BASE && addr < RAM_BASE + this.ram.length) {
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
    const start = addr >= 0x200000 ? 0x100000 : 0;
    const index = addr - start;
    if (size === 1) return this.rom[index] ?? 0;
    if (size === 2) return ((this.rom[index] ?? 0) << 8) | (this.rom[index + 1] ?? 0);
    return (
      ((this.rom[index] ?? 0) << 24) |
      ((this.rom[index + 1] ?? 0) << 16) |
      ((this.rom[index + 2] ?? 0) << 8) |
      (this.rom[index + 3] ?? 0)
    ) >>> 0;
  }
  writeMemory(addr, size, value) {
    const sr = this.getReg(M68kRegister.SR) & 0xffff;
    const srFlags = formatCcFlags(sr);
    const pcRaw = this.getReg(M68kRegister.PC) >>> 0;
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
    const pc = this.getReg(M68kRegister.PC) >>> 0;
    const sequence = ++this.sequence;
    if (!this.useTraceMem) {
      this.pendingWrites.push({
        addr,
        size,
        value,
        bytes: bytesOf(size, addr, value),
        pc,
        pcRaw,
        sr,
        srFlags,
        sequence,
        region: classifyAddr(addr)
      });
    }
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
  getRegisters() {
    const regs = {};
    for (let i = 0; i <= 19; i++) regs[i] = this.getReg(i);
    return regs;
  }
  step() {
    this.pendingWrites = [];
    this.traceWrites = [];
    const startPcRaw = this.getReg(M68kRegister.PC) >>> 0;
    const startPc = startPcRaw & 0xffffff;
    const startSr = this.getReg(M68kRegister.SR) & 0xffff;
    const cycles = this.module._m68k_step_one();
    const endPcRaw = this.getReg(M68kRegister.PC) >>> 0;
    const endPc = endPcRaw & 0xffffff;
    const endSr = this.getReg(M68kRegister.SR) & 0xffff;
    const writesRaw = this.useTraceMem
      ? this.traceWrites.slice()
      : this.pendingWrites.slice();
    const writes = writesRaw.filter((evt) => (evt.pcRaw >>> 0) === startPcRaw);
    return {
      startPc,
      startPcRaw,
      startSr,
      endPc,
      endPcRaw,
      endSr,
      cycles: Number(cycles) >>> 0,
      writes
    };
  }
}

class TpRunner {
  constructor(rom) {
    this.rom = rom;
    this.pendingWrites = [];
    this.sequence = 0;
  }
  async init() {
    const memoryLayout = {
      regions: [
        { start: 0x000000, length: 0x100000, source: 'rom', sourceOffset: 0x000000 },
        { start: 0x100000, length: 0x100000, source: 'ram' },
        { start: 0x200000, length: 0x100000, source: 'rom', sourceOffset: 0x100000 }
      ],
      minimumCapacity: ROM_LENGTH
    };
    this.system = await createTpSystem({
      rom: this.rom,
      ramSize: 0x100000,
      memoryLayout
    });
    this.unsubscribeWrite = this.system.onMemoryWrite((evt) => {
      const sequence = ++this.sequence;
      const regs = this.system.getRegisters();
      const sr = regs.sr & 0xffff;
      this.pendingWrites.push({
        addr: evt.addr >>> 0,
        size: evt.size,
        value: evt.value >>> 0,
        bytes: bytesOf(evt.size, evt.addr >>> 0, evt.value >>> 0),
        pc: evt.pc >>> 0,
        pcRaw: evt.pc >>> 0,
        sr,
        srFlags: formatCcFlags(sr),
        sequence,
        region: classifyAddr(evt.addr >>> 0)
      });
    });
  }
  reset() {
    this.system.reset();
  }
  setReg(register, value) {
    this.system.setRegister(register, value >>> 0);
  }
  getRegisters() {
    return this.system.getRegisters();
  }
  step() {
    this.pendingWrites = [];
    const regsBefore = this.system.getRegisters();
    const info = this.system.step();
    const regsAfter = this.system.getRegisters();
    const startPcRaw = info.startPc >>> 0;
    return {
      startPc: info.startPc >>> 0,
      startPcRaw,
      startSr: regsBefore.sr & 0xffff,
      endPc: info.endPc >>> 0,
      endPcRaw: info.endPc >>> 0,
      endSr: regsAfter.sr & 0xffff,
      cycles: Number(info.cycles) >>> 0,
      writes: this.pendingWrites
        .slice()
        .filter((evt) => (evt.pcRaw >>> 0) === startPcRaw)
    };
  }
}

const writeBytes = (buf, addr, bytes) => bytes.forEach((b, i) => (buf[addr + i] = b & 0xff));
const writeLongBE = (buf, addr, value) => {
  buf[addr + 0] = (value >>> 24) & 0xff;
  buf[addr + 1] = (value >>> 16) & 0xff;
  buf[addr + 2] = (value >>> 8) & 0xff;
  buf[addr + 3] = value & 0xff;
};

async function main() {
  const rom = new Uint8Array(ROM_LENGTH);
  writeLongBE(rom, 0x000000, STACK_BASE);
  writeLongBE(rom, 0x000004, CALL_ENTRY);
  if (ENABLE_VECTOR_SENTINELS) {
    for (let vec = 2; vec < 32; vec++) {
      writeLongBE(rom, vec * 4, 0xdead0000 | (vec & 0xffff));
    }
  }
  writeBytes(rom, MOVEM_PC, [0x48, 0xe7, 0xff, 0xfe]);
  writeBytes(rom, JSR1_PC, [0x4e, 0xb9, 0x00, 0x05, 0xdc, 0x1c]);
  writeBytes(rom, RETURN_PC, [0x4e, 0x75]);
  writeBytes(rom, TARGET_A, [0x30, 0x3c, 0x00, 0x9c]);
  writeBytes(rom, TARGET_A + 4, [0x21, 0xbc, 0xff, 0xff, 0xff, 0xff]);
  writeBytes(rom, TARGET_A + 10, [0x4e, 0x75]);

  const localRunner = new LocalRunner(rom);
  const tpRunner = new TpRunner(new Uint8Array(rom));

  await localRunner.init();
  await tpRunner.init();

  localRunner.reset();
  tpRunner.reset();

  const setLocal = (idx, value) => localRunner.setReg(idx, value >>> 0);
  setLocal(M68kRegister.A7, STACK_BASE);
  setLocal(M68kRegister.SP, STACK_BASE);
  setLocal(M68kRegister.A0, 0x00100a80);
  setLocal(M68kRegister.A1, 0x00100a80);
  setLocal(M68kRegister.D0, 0x0000009c);
  setLocal(M68kRegister.D1, 0);
  setLocal(M68kRegister.SR, 0x2704);
  setLocal(M68kRegister.PC, CALL_ENTRY);

  tpRunner.setReg('sp', STACK_BASE);
  tpRunner.setReg('pc', CALL_ENTRY);
  tpRunner.setReg('a0', 0x00100a80);
  tpRunner.setReg('a1', 0x00100a80);
  tpRunner.setReg('d0', 0x0000009c);
  tpRunner.setReg('d1', 0);
  tpRunner.setReg('sr', 0x2704);

  const stepLimit = 200000;
  for (let step = 0; step < stepLimit; step++) {
    const localStep = localRunner.step();
    const tpStep = tpRunner.step();

    const pcMismatch = localStep.startPcRaw !== tpStep.startPcRaw;
    const writeMapLocal = toByteMap(localStep.writes);
    const writeMapTp = toByteMap(tpStep.writes);
    const sizeMismatch = writeMapLocal.size !== writeMapTp.size;

    if (pcMismatch || sizeMismatch) {
      console.log('--- Fusion divergence detected ---');
      console.log(
        `Step ${step}` +
          `\n  Local: PC ${localStep.startPc.toString(16)} (raw ${localStep.startPcRaw
            .toString(16)}) -> ${localStep.endPc.toString(16)} (raw ${localStep.endPcRaw
            .toString(16)})` +
          `, SR ${localStep.startSr.toString(16)}->${localStep.endSr.toString(16)} [${formatCcFlags(localStep.startSr)}->${formatCcFlags(localStep.endSr)}]` +
          `, writes=${formatBytes(writeMapLocal)}` +
          `\n  TP   : PC ${tpStep.startPc.toString(16)} (raw ${tpStep.startPcRaw.toString(16)}) -> ${tpStep.endPc.toString(16)} (raw ${tpStep.endPcRaw
            .toString(16)})` +
          `, SR ${tpStep.startSr.toString(16)}->${tpStep.endSr.toString(16)} [${formatCcFlags(tpStep.startSr)}->${formatCcFlags(tpStep.endSr)}]` +
          `, writes=${formatBytes(writeMapTp)}`
      );
      if (sizeMismatch) {
        console.log(
          `Local musashi wrote ${writeMapLocal.size} bytes; TP wrote ${writeMapTp.size} bytes`
        );
      }
      const localRegs = localRunner.getRegisters();
      const formatLocal = (idx) => `0x${localRegs[idx].toString(16)}`;
      const tpRegs = tpRunner.getRegisters();
      const formatTp = (value) => `0x${value.toString(16)}`;
      console.log('Local registers:', {
        d0: formatLocal(M68kRegister.D0),
        d1: formatLocal(M68kRegister.D1),
        a0: formatLocal(M68kRegister.A0),
        a1: formatLocal(M68kRegister.A1),
        sp: formatLocal(M68kRegister.A7),
        sr: formatLocal(M68kRegister.SR),
        pc: formatLocal(M68kRegister.PC)
      });
      console.log('TP registers:', {
        d0: formatTp(tpRegs.d0),
        d1: formatTp(tpRegs.d1),
        a0: formatTp(tpRegs.a0),
        a1: formatTp(tpRegs.a1),
        sp: formatTp(tpRegs.sp),
        sr: formatTp(tpRegs.sr),
        pc: formatTp(tpRegs.pc)
      });
      const summarizeWrites = (label, writes) => {
        const totalBytes = writes.reduce((acc, evt) => acc + evt.bytes.length, 0);
        console.log(`${label} total bytes: ${totalBytes}`);
        for (const evt of writes) {
          const map = toByteMap([evt]);
          console.log(
            `  ${label} write[#${evt.sequence}] pc=0x${evt.pc
              .toString(16)} rawPc=0x${evt.pcRaw
              .toString(16)} sr=0x${evt.sr.toString(16)} [${evt.srFlags}]` +
              ` addr=0x${evt.addr.toString(16)} (${evt.region}) size=${evt.size} value=0x${evt.value
                .toString(16)} bytes=${formatBytes(map)}`
          );
        }
      };
      summarizeWrites('Local', localStep.writes);
      summarizeWrites('TP   ', tpStep.writes);
      console.log('\nMinimal reproduction complete.');
      return;
    }
  }
  console.log(`No divergence detected within ${stepLimit} instructions.`);
  const localRegs = localRunner.getRegisters();
  const tpRegs = tpRunner.getRegisters();
  console.log('Final Local PC:', `0x${localRegs[M68kRegister.PC].toString(16)}`);
  console.log('Final TP PC   :', `0x${tpRegs.pc.toString(16)}`);
}

main().catch((err) => {
  console.error(err);
  process.exitCode = 1;
});
