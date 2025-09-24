// This is a wrapper around the Emscripten module
import { M68kRegister } from '@m68k/common';
export class MusashiWrapper {
    constructor(module) {
        this._memory = new Uint8Array(0); // allocated in init()
        this._ramWindows = [];
        this._readFunc = 0;
        this._writeFunc = 0;
        this._probeFunc = 0;
        this._memTraceFunc = 0;
        this._memTraceActive = false;
        // No JS sentinel state; C++ session owns sentinel behavior.
        // CPU type for disassembler (68000)
        this.CPU_68000 = 0;
        this._module = module;
    }
    applyDefaultMemoryMapping(rom, ram) {
        this._memory.set(rom, 0x000000);
        this._memory.set(ram, 0x100000);
        this._ramWindows.push({ start: 0x100000, length: ram.length >>> 0, offset: 0 });
    }
    getActiveMemoryLayout(memoryLayout) {
        if (!memoryLayout) {
            return undefined;
        }
        const hasRegions = (memoryLayout.regions?.length ?? 0) > 0;
        const hasMirrors = (memoryLayout.mirrors?.length ?? 0) > 0;
        return hasRegions || hasMirrors ? memoryLayout : undefined;
    }
    init(system, rom, ram, memoryLayout) {
        this._system = system;
        const layout = this.getActiveMemoryLayout(memoryLayout);
        const requestedMinCapacity = (memoryLayout?.minimumCapacity ?? 0) >>> 0;
        // --- Allocate unified memory based on layout or defaults ---
        const DEFAULT_CAPACITY = 2 * 1024 * 1024; // 2MB
        let capacity = DEFAULT_CAPACITY >>> 0;
        this._ramWindows = [];
        if (layout) {
            let maxEnd = 0;
            for (const r of layout.regions ?? []) {
                const start = r.start >>> 0;
                const length = r.length >>> 0;
                if (length === 0)
                    continue;
                const end = (start + length) >>> 0;
                if (end > maxEnd)
                    maxEnd = end;
            }
            for (const m of layout.mirrors ?? []) {
                const destStart = m.start >>> 0;
                const destEnd = (destStart + (m.length >>> 0)) >>> 0;
                if (destEnd > maxEnd)
                    maxEnd = destEnd;
                const srcStart = m.mirrorFrom >>> 0;
                const srcEnd = (srcStart + (m.length >>> 0)) >>> 0;
                if (srcEnd > maxEnd)
                    maxEnd = srcEnd;
            }
            capacity = Math.max(maxEnd, requestedMinCapacity) >>> 0;
        }
        capacity = Math.max(capacity, requestedMinCapacity) >>> 0;
        this._memory = new Uint8Array(capacity);
        // --- Initialize regions ---
        if (layout) {
            // Base regions
            for (const r of layout.regions ?? []) {
                const start = r.start >>> 0;
                const length = r.length >>> 0;
                const srcOff = (r.sourceOffset ?? 0) >>> 0;
                if (length === 0)
                    continue;
                // Bounds validation
                if (start + length > this._memory.length) {
                    throw new Error(`Region out of bounds: start=0x${start.toString(16)}, len=0x${length.toString(16)}, cap=0x${this._memory.length.toString(16)}`);
                }
                if (r.source === 'rom') {
                    if (srcOff + length > rom.length) {
                        throw new Error(`ROM source out of range: off=0x${srcOff.toString(16)}, len=0x${length.toString(16)}, rom=0x${rom.length.toString(16)}`);
                    }
                    this._memory.set(rom.subarray(srcOff, srcOff + length), start);
                }
                else if (r.source === 'ram') {
                    if (srcOff + length > ram.length) {
                        throw new Error(`RAM source out of range: off=0x${srcOff.toString(16)}, len=0x${length.toString(16)}, ram=0x${ram.length.toString(16)}`);
                    }
                    this._memory.set(ram.subarray(srcOff, srcOff + length), start);
                    // Record window for write-back to RAM buffer
                    this._ramWindows.push({ start, length, offset: srcOff });
                }
                else if (r.source === 'zero') {
                    this._memory.fill(0, start, start + length);
                }
            }
            // Mirrors
            for (const m of layout.mirrors ?? []) {
                const start = m.start >>> 0;
                const length = m.length >>> 0;
                const from = m.mirrorFrom >>> 0;
                if (length === 0)
                    continue;
                if (from + length > this._memory.length || start + length > this._memory.length) {
                    throw new Error(`Mirror out of bounds: from=0x${from.toString(16)}, start=0x${start.toString(16)}, len=0x${length.toString(16)}, cap=0x${this._memory.length.toString(16)}`);
                }
                const src = this._memory.subarray(from, from + length);
                this._memory.set(src, start);
            }
        }
        else {
            // Backward-compatible default mapping
            this.applyDefaultMemoryMapping(rom, ram);
        }
        // Setup callbacks (size-aware read/write; PC hook)
        const readSizedPtr = this._module.addFunction((addr, size) => this.readHandler(addr >>> 0, (size | 0)), 'iii');
        const writeSizedPtr = this._module.addFunction((addr, size, val) => this.writeHandler(addr >>> 0, (size | 0), val >>> 0), 'viii');
        this._probeFunc = this._module.addFunction((addr) => (this._system._handlePCHook(addr >>> 0) ? 1 : 0), 'ii');
        this._readFunc = readSizedPtr;
        this._writeFunc = writeSizedPtr;
        // Register callbacks with C
        this._module._set_read_mem_func(readSizedPtr);
        this._module._set_write_mem_func(writeSizedPtr);
        this._module._set_pc_hook_func(this._probeFunc);
        // CRITICAL: Write reset vectors BEFORE init/reset
        this.write32BE(0x00000000, 0x00108000); // Initial SSP (in RAM)
        this.write32BE(0x00000004, 0x00000400); // Initial PC (in ROM)
        // Initialize CPU
        this._module._m68k_init();
        this._module._m68k_set_context?.(0);
        // Reset CPU (will read vectors we just wrote)
        this._module._m68k_pulse_reset();
        // Verify initialization
        const pc = this._module._m68k_get_reg(0, M68kRegister.PC);
        if (pc !== 0x400) {
            throw new Error(`CPU not properly initialized, PC=0x${pc.toString(16)} (expected 0x400)`);
        }
    }
    write32BE(addr, value) {
        this._memory[addr + 0] = (value >>> 24) & 0xff;
        this._memory[addr + 1] = (value >>> 16) & 0xff;
        this._memory[addr + 2] = (value >>> 8) & 0xff;
        this._memory[addr + 3] = value & 0xff;
    }
    cleanup() {
        // Clean up function pointers
        if (this._readFunc) {
            this._module.removeFunction?.(this._readFunc);
            this._readFunc = 0;
        }
        if (this._writeFunc) {
            this._module.removeFunction?.(this._writeFunc);
            this._writeFunc = 0;
        }
        if (this._probeFunc) {
            this._module.removeFunction?.(this._probeFunc);
            this._probeFunc = 0;
        }
        if (this._memTraceFunc) {
            // Clear callback in core before removing to avoid dangling ptr
            this._module._m68k_set_trace_mem_callback?.(0);
            this._module.removeFunction?.(this._memTraceFunc);
            this._memTraceFunc = 0;
            this._memTraceActive = false;
        }
        this._module._clear_regions?.();
        this._module._clear_pc_hook_addrs?.();
        try {
            this._module._set_pc_hook_func?.(0);
        }
        catch { }
        this._module._reset_myfunc_state?.();
    }
    readHandler(address, size) {
        // The regions should handle most reads, but this is a fallback
        return this._system.read(address, size);
    }
    writeHandler(address, size, value) {
        // The regions should handle most writes, but this is a fallback
        this._system.write(address, size, value);
    }
    pcHookHandler(pc) {
        // Delegate stop/continue decision to SystemImpl. The C++ session will
        // handle vectoring to the sentinel when we return non-zero here.
        return this._system._handlePCHook(pc) ? 1 : 0;
    }
    execute(cycles) {
        return this._module._m68k_execute(cycles);
    }
    step() {
        return this._module._m68k_step_one();
    }
    requireExport(name) {
        const val = this._module[name];
        if (typeof val !== 'function') {
            throw new Error(`${String(name)} is not available; rebuild WASM exports`);
        }
    }
    call(address) {
        // Rely on C++-side session that honors JS PC hooks and vectors to a
        // sentinel (max address) when JS requests a stop. This keeps nested
        // calls safe without opcode heuristics.
        // Ensure export exists for type safety, then cast and call
        this.requireExport('_m68k_call_until_js_stop');
        const callUntil = this._module._m68k_call_until_js_stop;
        // Defer timeslice to C++ default by passing 0
        const ret = callUntil(address >>> 0, 0);
        return typeof ret === 'bigint' ? Number(ret) >>> 0 : ret >>> 0;
    }
    // Expose break reason helpers for tests
    getLastBreakReason() {
        return typeof this._module._m68k_get_last_break_reason === 'function'
            ? this._module._m68k_get_last_break_reason() >>> 0
            : 0;
    }
    resetLastBreakReason() {
        this._module._m68k_reset_last_break_reason?.();
    }
    pulse_reset() {
        this._module._m68k_pulse_reset();
    }
    get_reg(index) {
        return this._module._m68k_get_reg(0, index) >>> 0;
    }
    set_reg(index, value) {
        this._module._m68k_set_reg(index, value);
    }
    add_pc_hook_addr(addr) {
        this._module._add_pc_hook_addr(addr);
    }
    findRamWindowForAddress(address) {
        const addr = address >>> 0;
        for (const window of this._ramWindows) {
            const start = window.start >>> 0;
            const end = start + (window.length >>> 0);
            if (addr >= start && addr < end) {
                return window;
            }
        }
        return undefined;
    }
    isAccessWithinMemory(address, size) {
        const addr = address >>> 0;
        if (addr >= this._memory.length)
            return false;
        const end = addr + size;
        if (end > this._memory.length)
            return false;
        const window = this.findRamWindowForAddress(addr);
        if (window && end > window.start + window.length) {
            return false;
        }
        return true;
    }
    read_memory(address, size) {
        const addr = address >>> 0;
        // Read from our unified memory (big-endian composition)
        if (!this.isAccessWithinMemory(addr, size))
            return 0;
        if (size === 1) {
            return this._memory[addr];
        }
        else if (size === 2) {
            return (this._memory[addr] << 8) | this._memory[addr + 1];
        }
        else {
            return ((this._memory[addr] << 24) |
                (this._memory[addr + 1] << 16) |
                (this._memory[addr + 2] << 8) |
                this._memory[addr + 3]) >>> 0;
        }
    }
    write_memory(address, size, value) {
        const addr = address >>> 0;
        // Respect bounds strictly: ignore cross-boundary writes
        if (!this.isAccessWithinMemory(addr, size))
            return;
        const maskedValue = value >>> 0;
        const hasWindows = this._ramWindows.length > 0;
        const ram = this._system.ram;
        for (let i = 0; i < size; i++) {
            const shift = (size - 1 - i) * 8;
            const byte = (maskedValue >> shift) & 0xff;
            const a = (addr + i) >>> 0;
            this._memory[a] = byte;
            if (!hasWindows)
                continue;
            const window = this.findRamWindowForAddress(a);
            if (!window)
                continue;
            const ramIndex = (window.offset + (a - window.start)) >>> 0;
            if (ramIndex < ram.length) {
                ram[ramIndex] = byte;
            }
        }
    }
    // --- Memory Trace Hook Bridge ---
    // Enable/disable forwarding of core memory trace events to SystemImpl
    setMemoryTraceEnabled(enable) {
        if (!this._module._m68k_set_trace_mem_callback || !this._module._m68k_trace_enable || !this._module._m68k_trace_set_mem_enabled) {
            // Not supported in this build
            return;
        }
        if (enable && !this._memTraceActive) {
            if (!this._memTraceFunc) {
                // Signature from m68ktrace.h:
                // int (*m68k_trace_mem_callback)(m68k_trace_mem_type type,
                //   uint32_t pc, uint32_t address, uint32_t value, uint8_t size, uint64_t cycles);
                // Prefer BigInt signature when available ('iiiiij');
                // Fallback to number-only builds ('iiiiiii') where cycles is split/lost.
                const makeCb = () => (type, pc, addr, value, size, _cycles) => {
                    const s = (size | 0);
                    const a = addr >>> 0;
                    const v = value >>> 0;
                    const p = pc >>> 0;
                    if (type === 0) {
                        this._system._handleMemoryRead?.(a, s, v, p);
                    }
                    else {
                        this._system._handleMemoryWrite?.(a, s, v, p);
                    }
                    return 0;
                };
                try {
                    this._memTraceFunc = this._module.addFunction(makeCb(), 'iiiiiij');
                }
                catch {
                    this._memTraceFunc = this._module.addFunction(makeCb(), 'iiiiiii');
                }
            }
            // Wire into core and turn on tracing
            this._module._m68k_set_trace_mem_callback(this._memTraceFunc);
            this._module._m68k_trace_enable(1);
            this._module._m68k_trace_set_mem_enabled(1);
            this._memTraceActive = true;
        }
        else if (!enable && this._memTraceActive) {
            this._module._m68k_set_trace_mem_callback(0);
            this._module._m68k_trace_set_mem_enabled(0);
            this._memTraceActive = false;
        }
    }
    /**
     * Disassembles a single instruction at the given address.
     * Returns null if the underlying module does not expose the disassembler.
     */
    disassemble(address) {
        const mod = this._module;
        if (typeof mod._m68k_disassemble !== 'function' || !mod._malloc || !mod._free) {
            return null;
        }
        // Allocate a small buffer for the disassembly string
        const CAP = 256;
        const buf = mod._malloc(CAP);
        try {
            const size = mod._m68k_disassemble(buf, address >>> 0, this.CPU_68000) >>> 0;
            // Read C-string from HEAPU8 until NUL or CAP
            const heap = mod.HEAPU8;
            let s = '';
            for (let i = buf; i < buf + CAP; i++) {
                const c = heap[i];
                if (c === 0)
                    break;
                s += String.fromCharCode(c);
            }
            return { text: s, size };
        }
        catch {
            return null;
        }
        finally {
            try {
                mod._free(buf);
            }
            catch {
                /* ignore */
            }
        }
    }
    /**
     * Convenience helper to disassemble a short sequence starting at address.
     */
    disassembleSequence(address, count) {
        const out = [];
        let pc = address >>> 0;
        for (let i = 0; i < count; i++) {
            const one = this.disassemble(pc);
            if (!one)
                break;
            out.push({ pc, text: one.text, size: one.size >>> 0 });
            pc = (pc + (one.size >>> 0)) >>> 0;
            if (one.size === 0)
                break; // avoid infinite loop on malformed decode
        }
        return out;
    }
    // --- Perfetto Tracing Wrappers ---
    isPerfettoAvailable() {
        return typeof this._module._m68k_perfetto_init === 'function';
    }
    withHeapString(value, fn) {
        const malloc = this._module._malloc;
        const free = this._module._free;
        const heap = this._module.HEAPU8;
        if (typeof malloc !== 'function' ||
            typeof free !== 'function' ||
            !(heap instanceof Uint8Array)) {
            throw new Error('Musashi module does not expose heap string helpers.');
        }
        const ptr = malloc(value.length + 1);
        for (let i = 0; i < value.length; i++) {
            heap[ptr + i] = value.charCodeAt(i) & 0xff;
        }
        heap[ptr + value.length] = 0;
        try {
            return fn(ptr);
        }
        finally {
            free(ptr);
        }
    }
    perfettoInit(processName) {
        const init = this._module._m68k_perfetto_init;
        if (!init)
            return -1;
        return this.withHeapString(processName, (namePtr) => init(namePtr));
    }
    perfettoDestroy() {
        this._module._m68k_perfetto_destroy?.();
    }
    perfettoCleanupSlices() {
        this._module._m68k_perfetto_cleanup_slices?.();
    }
    perfettoEnableFlow(enable) {
        this._module._m68k_perfetto_enable_flow?.(enable ? 1 : 0);
    }
    perfettoEnableMemory(enable) {
        this._module._m68k_perfetto_enable_memory?.(enable ? 1 : 0);
    }
    perfettoEnableInstructions(enable) {
        this._module._m68k_perfetto_enable_instructions?.(enable ? 1 : 0);
    }
    traceEnable(enable) {
        this._module._m68k_trace_enable?.(enable ? 1 : 0);
    }
    perfettoIsInitialized() {
        if (!this._module._m68k_perfetto_is_initialized)
            return false;
        return this._module._m68k_perfetto_is_initialized() !== 0;
    }
    _registerSymbol(func, address, name) {
        this.withHeapString(name, (namePtr) => {
            func.call(this._module, address, namePtr);
        });
    }
    registerFunctionName(address, name) {
        if (this._module._register_function_name) {
            this._registerSymbol(this._module._register_function_name.bind(this._module), address, name);
        }
        // Note: ccall fallback removed as it's not typically exported
    }
    registerMemoryName(address, name) {
        if (this._module._register_memory_name) {
            this._registerSymbol(this._module._register_memory_name.bind(this._module), address, name);
        }
        // Note: ccall fallback removed as it's not typically exported
    }
    perfettoExportTrace() {
        if (!this._module._m68k_perfetto_export_trace)
            return null;
        const dataPtrPtr = this._module._malloc(4);
        const sizePtr = this._module._malloc(4);
        try {
            const result = this._module._m68k_perfetto_export_trace(dataPtrPtr, sizePtr);
            if (result !== 0)
                return null;
            // Read pointer values from WASM heap
            const dataPtr = this._module.HEAP32[dataPtrPtr >> 2];
            const dataSize = this._module.HEAP32[sizePtr >> 2];
            if (dataPtr === 0 || dataSize === 0) {
                return new Uint8Array(0);
            }
            const traceData = new Uint8Array(this._module.HEAPU8.subarray(dataPtr, dataPtr + dataSize));
            this._module._m68k_perfetto_free_trace_data(dataPtr);
            return traceData;
        }
        finally {
            this._module._free(dataPtrPtr);
            this._module._free(sizePtr);
        }
    }
}
// Factory function to load and initialize the wasm module
export async function getModule() {
    // Environment detection without DOM types
    const isNode = typeof process !== 'undefined' && !!process.versions?.node;
    // Dynamic import based on environment
    let module;
    if (isNode) {
        // For Node.js, use the ESM wrapper
        // @ts-ignore - Dynamic import of .mjs file
        const { default: createMusashiModule } = await import('../wasm/musashi-node-wrapper.mjs');
        module = await createMusashiModule();
        // Runtime validation to catch shape mismatches early
        const modMaybe = module;
        if (!modMaybe || typeof modMaybe._m68k_init !== 'function') {
            const keys = module && typeof module === 'object' ? Object.keys(module).slice(0, 20) : [];
            throw new Error('Musashi Module shape unexpected: _m68k_init not found. ' +
                `Type=${typeof module}, keys=${JSON.stringify(keys)}`);
        }
    }
    else {
        // For browser, import the web ESM version
        // Use variable specifier to avoid TS2307 compile-time resolution
        const specifier = '../../../musashi.out.mjs';
        // @ts-ignore - Dynamic import of .mjs file
        const mod = await import(/* webpackIgnore: true */ specifier);
        const moduleFactory = mod.default;
        module = await moduleFactory();
    }
    return new MusashiWrapper(module);
}
//# sourceMappingURL=musashi-wrapper.js.map