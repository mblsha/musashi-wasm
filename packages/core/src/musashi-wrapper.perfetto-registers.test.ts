import { jest } from '@jest/globals';
import { MusashiWrapper } from './musashi-wrapper.js';

const createStubModule = (
  overrides: Record<string, unknown> = {}
): Record<string, unknown> => ({
  HEAPU8: new Uint8Array(0),
  HEAP32: new Int32Array(0),
  HEAPU32: new Uint32Array(0),
  _m68k_set_trace_mem_callback: () => {},
  _m68k_trace_enable: () => {},
  _m68k_trace_set_mem_enabled: () => {},
  _malloc: () => 0,
  _free: () => {},
  ...overrides,
});

describe('MusashiWrapper.perfettoEnableInstructionRegisters', () => {
  it('throws when enabling without wasm support', () => {
    const wrapper = new MusashiWrapper(createStubModule() as any);

    expect(() => wrapper.perfettoEnableInstructionRegisters(false)).not.toThrow();
    expect(() => wrapper.perfettoEnableInstructionRegisters(true)).toThrow(
      /instruction register tracing is not available/i
    );
  });

  it('invokes the wasm export when available', () => {
    const enableMock = jest.fn();
    const wrapper = new MusashiWrapper(
      createStubModule({
        _m68k_perfetto_enable_instruction_registers: enableMock,
      }) as any
    );

    wrapper.perfettoEnableInstructionRegisters(true);
    wrapper.perfettoEnableInstructionRegisters(false);

    expect(enableMock).toHaveBeenNthCalledWith(1, 1);
    expect(enableMock).toHaveBeenNthCalledWith(2, 0);
  });
});
