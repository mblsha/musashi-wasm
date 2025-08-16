// Mock implementation of musashi-wrapper for testing

export class MusashiWrapper {
  private _memory: Uint8Array;
  private _registers: number[] = new Array(32).fill(0);
  private _cyclesRun = 0;
  private _pcHooks = new Map<number, (addr: number) => boolean>();
  private _overrideHooks = new Map<number, (addr: number) => boolean>();
  private _system: any = null;
  private _ramBase = 0x100000;
  private _ramSize = 0;

  constructor() {
    // Initialize memory to zeros
    this._memory = new Uint8Array(2 * 1024 * 1024);
    this._memory.fill(0);
  }

  init(system: any, rom: Uint8Array, ram: Uint8Array) {
    this._system = system;
    // Set ROM at address 0
    this._memory.set(rom, 0x000000);
    // Store RAM size for later use
    this._ramSize = ram.length;
    // RAM area starts at _ramBase (0x100000), so clear that region
    for (let i = 0; i < this._ramSize; i++) {
      this._memory[this._ramBase + i] = 0;
    }
    // Set initial PC and SP from reset vector
    this._registers[15] = this.read32BE(0);  // SP
    this._registers[16] = this.read32BE(4);  // PC
  }

  reset() {
    this._registers[15] = this.read32BE(0);  // SP
    this._registers[16] = this.read32BE(4);  // PC
  }

  pulse_reset() {
    // Alias for reset in mock
    this.reset();
  }

  execute(cycles: number): number {
    // Simple mock: simulate executing instructions
    let remainingCycles = cycles;
    
    while (remainingCycles > 0) {
      const pc = this._registers[16];
      
      // Call the system's PC hook handler if it's a hooked address
      if (this._system && (this._pcHooks.has(pc) || this._overrideHooks.has(pc))) {
        if (this._system._handlePCHook(pc)) {
          // Override requested stop with RTS
          return cycles - remainingCycles;
        }
      }
      
      // Simulate instruction execution
      this._registers[16] += 2;  // Advance PC by 2 (simple instruction)
      remainingCycles -= 4;  // Each instruction takes some cycles
      this._cyclesRun += 4;
      
      // Stop if we've executed enough cycles
      if (remainingCycles <= 0) break;
    }
    
    return cycles;
  }

  call(address: number): number {
    // Mock implementation of subroutine call
    const oldPc = this._registers[16];
    this._registers[16] = address;
    
    // Simulate subroutine execution (simplified)
    const cycles = 50;  // Mock cycles for a subroutine
    this._cyclesRun += cycles;
    
    // Return to saved PC (simplified)
    this._registers[16] = oldPc + 4;
    
    return cycles;
  }

  get_reg(index: number): number {
    return this._registers[index] || 0;
  }

  set_reg(index: number, value: number) {
    this._registers[index] = value;
  }

  read_memory(address: number, size: 1 | 2 | 4): number {
    // Map RAM addresses to the correct location in our memory array
    if (address >= this._ramBase && address < this._ramBase + this._ramSize) {
      // RAM access - already mapped to the right location
    }
    // Otherwise use address directly (ROM area)
    
    switch (size) {
      case 1:
        return this._memory[address] || 0;
      case 2:
        return ((this._memory[address] || 0) << 8) | (this._memory[address + 1] || 0);
      case 4:
        return this.read32BE(address);
    }
  }

  write_memory(address: number, size: 1 | 2 | 4, value: number) {
    // Map RAM addresses to the correct location in our memory array
    if (address >= this._ramBase && address < this._ramBase + this._ramSize) {
      // RAM access - already mapped to the right location
    }
    // Otherwise use address directly (ROM area)
    
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
    // Track that this address has a hook
    // The actual callbacks are managed by the System class
    if (!this._pcHooks.has(addr)) {
      this._pcHooks.set(addr, () => false);
    }
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

  perfettoCleanupSlices() {
    // No-op in mock
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

// Also export default for compatibility
export default { getModule, MusashiWrapper };