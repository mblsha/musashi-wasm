declare module 'musashi-wasm' {
  export interface MusashiModule {
    _m68k_init(): void;
    _m68k_execute(cycles: number): number;
    _m68k_pulse_reset(): void;
    _m68k_cycles_run(): number;
    _m68k_get_reg(context: number | null, reg: number): number;
    _m68k_set_reg(reg: number, value: number): void;
    _add_region(base: number, size: number, dataPtr: number): void;
    _clear_regions(): void;
    _set_read_mem_func(ptr: number): void;
    _set_write_mem_func(ptr: number): void;
    _set_pc_hook_func(ptr: number): void;
    _enable_printf_logging(enable: number): void;
    _malloc(size: number): number;
    _free(ptr: number): void;
    HEAPU8: Uint8Array;
    addFunction(func: Function, signature: string): number;
    removeFunction(ptr: number): void;
    getValue(ptr: number, type: string): number;
    allocateUTF8(str: string): number;
  }

  export enum M68kRegister {
    D0 = 0,
    D1 = 1,
    D2 = 2,
    D3 = 3,
    D4 = 4,
    D5 = 5,
    D6 = 6,
    D7 = 7,
    A0 = 8,
    A1 = 9,
    A2 = 10,
    A3 = 11,
    A4 = 12,
    A5 = 13,
    A6 = 14,
    A7 = 15,
    PC = 16,
    SR = 17,
    SP = 18,
    USP = 19,
    ISP = 20,
    MSP = 21,
    SFC = 22,
    DFC = 23,
    VBR = 24,
    CACR = 25,
    CAAR = 26,
    PREF_ADDR = 27,
    PREF_DATA = 28,
    PPC = 29,
    IR = 30,
    CPU_TYPE = 31
  }

  export type ReadMemoryCallback = (address: number) => number;
  export type WriteMemoryCallback = (address: number, value: number) => void;
  export type PCHookCallback = (address: number) => number;

  export class Musashi {
    constructor();
    
    /**
     * Initialize the M68k emulator. Must be called before any other operations.
     */
    init(): Promise<void>;
    
    /**
     * Execute CPU instructions for the specified number of cycles.
     * @param cycles Number of cycles to execute
     * @returns Number of cycles actually executed
     */
    execute(cycles: number): number;
    
    /**
     * Pulse the RESET line of the CPU.
     */
    pulseReset(): void;
    
    /**
     * Get the number of cycles executed so far.
     * @returns Number of cycles run
     */
    cyclesRun(): number;
    
    /**
     * Get the value of a CPU register.
     * @param reg Register identifier from M68kRegister enum
     * @returns Register value
     */
    getReg(reg: M68kRegister): number;
    
    /**
     * Set the value of a CPU register.
     * @param reg Register identifier from M68kRegister enum
     * @param value Value to set
     */
    setReg(reg: M68kRegister, value: number): void;
    
    /**
     * Add a memory region to the emulator.
     * @param base Base address of the region
     * @param size Size of the region in bytes
     * @param dataPtr Pointer to the memory data in WASM heap
     */
    addRegion(base: number, size: number, dataPtr: number): void;
    
    /**
     * Clear all memory regions.
     */
    clearRegions(): void;
    
    /**
     * Set the memory read callback function.
     * @param func Callback function that reads memory
     * @returns Function pointer that can be used with removeFunction
     */
    setReadMemFunc(func: ReadMemoryCallback): number;
    
    /**
     * Set the memory write callback function.
     * @param func Callback function that writes memory
     * @returns Function pointer that can be used with removeFunction
     */
    setWriteMemFunc(func: WriteMemoryCallback): number;
    
    /**
     * Set the PC hook callback function.
     * @param func Callback function called on each instruction
     * @returns Function pointer that can be used with removeFunction
     */
    setPCHookFunc(func: PCHookCallback): number;
    
    /**
     * Remove a previously registered callback function.
     * @param ptr Function pointer returned by set*Func methods
     */
    removeFunction(ptr: number): void;
    
    /**
     * Allocate memory in the WASM heap.
     * @param size Number of bytes to allocate
     * @returns Pointer to allocated memory
     */
    allocateMemory(size: number): number;
    
    /**
     * Free memory in the WASM heap.
     * @param ptr Pointer to memory to free
     */
    freeMemory(ptr: number): void;
    
    /**
     * Write data to WASM heap memory.
     * @param ptr Pointer to destination in WASM heap
     * @param data Data to write
     */
    writeMemory(ptr: number, data: Uint8Array): void;
    
    /**
     * Read data from WASM heap memory.
     * @param ptr Pointer to source in WASM heap
     * @param size Number of bytes to read
     * @returns Data read from memory
     */
    readMemory(ptr: number, size: number): Uint8Array;
    
    /**
     * Enable or disable printf logging.
     * @param enable True to enable, false to disable
     */
    enablePrintfLogging(enable: boolean): void;
  }

  export default Musashi;
}