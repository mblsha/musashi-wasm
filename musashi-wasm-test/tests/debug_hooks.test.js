import { jest } from '@jest/globals';
import createMusashiModule from '../../musashi-node.out.mjs';

describe('Debug Hook Issues', () => {
  let Module;
  let memPtr;
  const memSize = 0x10000;

  beforeEach(async () => {
    Module = await createMusashiModule();
    
    // Initialize CPU
    Module._m68k_init();
    
    // Allocate memory
    memPtr = Module._malloc(memSize);
    Module.HEAPU8.fill(0, memPtr, memPtr + memSize);
    
    // Add memory region
    Module._add_region(0x0, memSize, memPtr);
    
    // Set up reset vectors
    const view = new DataView(Module.HEAPU8.buffer, memPtr, memSize);
    view.setUint32(0, 0x8000, false);  // SP at 0
    view.setUint32(4, 0x100, false);   // PC at 4
    
    // Write a NOP instruction at PC location
    view.setUint16(0x100, 0x4E71, false); // NOP
    
    // Reset CPU
    Module._m68k_pulse_reset();
  });

  afterEach(() => {
    if (Module && memPtr) {
      Module._clear_regions();
      Module._free(memPtr);
      Module._reset_myfunc_state();
    }
  });

  test('Simple PC hook test', () => {
    console.log('Starting simple PC hook test...');
    
    const pcValues = [];
    const pcHook = Module.addFunction((pc) => {
      console.log(`PC Hook called with PC=0x${pc.toString(16)}`);
      pcValues.push(pc);
      return 0; // Continue
    }, 'ii');
    
    console.log(`PC hook function pointer: ${pcHook}`);
    
    Module._set_pc_hook_func(pcHook);
    Module._add_pc_hook_addr(0x0); // Hook all addresses
    
    console.log('Executing CPU for 10 cycles...');
    const result = Module._m68k_execute(10);
    console.log(`Execute result: ${result}`);
    
    console.log(`PC values captured: ${pcValues.map(pc => '0x' + pc.toString(16)).join(', ')}`);
    
    // Check current PC
    const currentPC = Module._m68k_get_reg(null, 16); // M68K_REG_PC
    console.log(`Current PC after execution: 0x${currentPC.toString(16)}`);
    
    expect(pcValues.length).toBeGreaterThan(0);
    
    Module._clear_pc_hook_func();
    Module.removeFunction(pcHook);
  });

  test('Simple instruction hook test', () => {
    console.log('Starting simple instruction hook test...');
    
    const instrCalls = [];
    const instrHook = Module.addFunction((pc, ir, cycles) => {
      console.log(`Instruction Hook: PC=0x${pc.toString(16)}, IR=0x${ir.toString(16)}, cycles=${cycles}`);
      instrCalls.push({ pc, ir, cycles });
      return 0; // Continue
    }, 'iiii');
    
    console.log(`Instruction hook function pointer: ${instrHook}`);
    
    Module._set_full_instr_hook_func(instrHook);
    
    console.log('Executing CPU for 10 cycles...');
    const result = Module._m68k_execute(10);
    console.log(`Execute result: ${result}`);
    
    console.log(`Instructions captured: ${instrCalls.length}`);
    instrCalls.forEach((call, i) => {
      console.log(`  [${i}] PC=0x${call.pc.toString(16)}, IR=0x${call.ir.toString(16)}, cycles=${call.cycles}`);
    });
    
    expect(instrCalls.length).toBeGreaterThan(0);
    
    Module._clear_instr_hook_func();
    Module.removeFunction(instrHook);
  });

  test('Check if hooks are actually being invoked from C', () => {
    console.log('Testing hook invocation...');
    
    // Enable printf logging to see C debug output
    Module._enable_printf_logging();
    
    let hookCalled = false;
    const testHook = Module.addFunction((pc) => {
      console.log(`*** HOOK CALLED! PC=0x${pc.toString(16)} ***`);
      hookCalled = true;
      return 0;
    }, 'ii');
    
    Module._set_pc_hook_func(testHook);
    Module._add_pc_hook_addr(0x100); // Hook specific address
    
    console.log('About to execute...');
    Module._m68k_execute(20);
    
    expect(hookCalled).toBe(true);
    
    Module._clear_pc_hook_func();
    Module.removeFunction(testHook);
  });
});