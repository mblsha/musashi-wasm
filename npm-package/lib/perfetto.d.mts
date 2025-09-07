declare module 'musashi-wasm/perfetto' {
  import { Musashi, M68kRegister, ReadMemoryCallback, WriteMemoryCallback, PCHookCallback } from 'musashi-wasm';

  export class MusashiPerfetto extends Musashi {
    constructor();
    
    /**
     * Initialize the M68k emulator with Perfetto tracing support.
     * @param processName Name to use for the process in traces (default: 'Musashi')
     */
    init(processName?: string): Promise<void>;
    
    /**
     * Enable or disable flow tracing.
     * @param enable True to enable, false to disable
     */
    enableFlowTracing(enable: boolean): void;
    
    /**
     * Enable or disable instruction tracing.
     * @param enable True to enable, false to disable
     */
    enableInstructionTracing(enable: boolean): void;
    
    /**
     * Enable or disable memory access tracing.
     * @param enable True to enable, false to disable
     */
    enableMemoryTracing(enable: boolean): void;
    
    /**
     * Enable or disable interrupt tracing.
     * @param enable True to enable, false to disable
     */
    enableInterruptTracing(enable: boolean): void;
    
    /**
     * Export the current trace data as a Uint8Array.
     * @returns Trace data in Perfetto format, or null if no trace data
     */
    exportTrace(): Promise<Uint8Array | null>;
    
    /**
     * Save the current trace data to a file.
     * @param filename Path to save the trace file
     * @returns True if saved successfully, false otherwise
     */
    saveTrace(filename: string): Promise<boolean>;
  }

  export { M68kRegister, ReadMemoryCallback, WriteMemoryCallback, PCHookCallback };
  export default MusashiPerfetto;
}