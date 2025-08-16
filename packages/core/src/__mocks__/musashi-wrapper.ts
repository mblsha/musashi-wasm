// Mock implementation of musashi-wrapper for testing

export class MusashiWrapper {
  private _memory: Uint8Array = new Uint8Array(2 * 1024 * 1024);
  private _registers: number[] = new Array(32).fill(0);
  private _cyclesRun = 0;
  private _pcHooks = new Map<number, (addr: number) => boolean>();
  private _overrideHooks = new Map<number, (addr: number) => boolean>();

  init(system: any, rom: Uint8Array, ram: Uint8Array) {
    this._memory.set(rom, 0x000000);
    // RAM is handled by size in config, not passed directly anymore
    // Set initial PC and SP from reset vector
    this._registers[15] = this.read32BE(0);  // SP
    this._registers[16] = this.read32BE(4);  // PC
  }

  reset() {
    this._registers[15] = this.read32BE(0);  // SP
    this._registers[16] = this.read32BE(4);  // PC
  }

  execute(cycles: number): number {
    // Simple mock: just increment PC and return cycles
    const pc = this._registers[16];
    
    // Check for probe hooks
    if (this._pcHooks.has(pc)) {
      const hook = this._pcHooks.get(pc)!;
      if (hook(pc)) {
        // Hook requested stop
        return cycles;
      }
    }
    
    // Check for override hooks  
    if (this._overrideHooks.has(pc)) {
      const hook = this._overrideHooks.get(pc)!;
      hook(pc);
      // Override replaces instruction, simulate RTS
      return cycles;
    }
    
    this._registers[16] += 2;
    this._cyclesRun += cycles;
    
    return cycles;
  }

  get_reg(index: number): number {
    return this._registers[index] || 0;
  }

  set_reg(index: number, value: number) {
    this._registers[index] = value;
  }

  read_memory(address: number, size: 1 | 2 | 4): number {
    switch (size) {
      case 1:
        return this._memory[address] || 0;
      case 2:
        return ((this._memory[address] || 0) << 8) | (this._memory[address + 1] || 0);
      case 4:
        return this.read32BE(address);
    }
  }

  write_memory(address: number, value: number, size: 1 | 2 | 4) {
    switch (size) {
      case 1:
        this._memory[address] = value & 0xFF;
        break;
      case 2:
        this._memory[address] = (value >> 8) & 0xFF;
        this._memory[address + 1] = value & 0xFF;
        break;
      case 4:
        this.write32BE(address, value);
        break;
    }
  }

  add_pc_hook_addr(addr: number) {
    // Mock implementation
  }

  set_probe_hook(addr: number, hook: (addr: number) => boolean) {
    this._pcHooks.set(addr, hook);
  }

  remove_probe_hook(addr: number) {
    this._pcHooks.delete(addr);
  }

  set_override_hook(addr: number, hook: (addr: number) => boolean) {
    this._overrideHooks.set(addr, hook);
  }

  remove_override_hook(addr: number) {
    this._overrideHooks.delete(addr);
  }

  // Perfetto mock methods
  isPerfettoAvailable(): boolean {
    return false;  // Mock doesn't have Perfetto
  }

  perfettoInit(name: string): number {
    return -1;  // Not available
  }

  perfettoDestroy() {}
  
  perfettoEnableFlow(enable: boolean) {}
  perfettoEnableMemory(enable: boolean) {}
  perfettoEnableInstructions(enable: boolean) {}
  
  perfettoExportTrace(): Uint8Array | null {
    return null;
  }

  traceEnable(enable: boolean) {}

  registerFunctionName(address: number, name: string) {}
  registerMemoryName(address: number, name: string) {}
  
  cleanup() {
    // Reset state
    this._memory.fill(0);
    this._registers.fill(0);
    this._cyclesRun = 0;
    this._pcHooks.clear();
    this._overrideHooks.clear();
  }

  private read32BE(address: number): number {
    return ((this._memory[address] || 0) << 24) |
           ((this._memory[address + 1] || 0) << 16) |
           ((this._memory[address + 2] || 0) << 8) |
           (this._memory[address + 3] || 0);
  }

  private write32BE(address: number, value: number) {
    this._memory[address] = (value >> 24) & 0xFF;
    this._memory[address + 1] = (value >> 16) & 0xFF;
    this._memory[address + 2] = (value >> 8) & 0xFF;
    this._memory[address + 3] = value & 0xFF;
  }
}

export async function getModule(): Promise<MusashiWrapper> {
  return new MusashiWrapper();
}