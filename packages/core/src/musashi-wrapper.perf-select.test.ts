import { jest } from '@jest/globals';

type StubModuleOptions = {
  perfetto?: boolean;
};

const createStubModule = ({ perfetto = false }: StubModuleOptions = {}) => {
  const base = {
    _m68k_init: jest.fn(),
    _m68k_set_trace_mem_callback: jest.fn(),
    _m68k_trace_enable: jest.fn(),
    _m68k_trace_set_mem_enabled: jest.fn(),
    HEAPU8: new Uint8Array(0),
    HEAP32: new Int32Array(0),
    addFunction: jest.fn(() => 1),
    removeFunction: jest.fn(),
    _malloc: jest.fn(() => 0),
    _free: jest.fn(),
    _set_read_mem_func: jest.fn(),
    _set_write_mem_func: jest.fn(),
    _set_pc_hook_func: jest.fn(),
    _clear_regions: jest.fn(),
    _clear_pc_hook_addrs: jest.fn(),
    _m68k_pulse_reset: jest.fn(),
    _m68k_get_reg: jest.fn(() => 0),
  };

  if (perfetto) {
    return {
      ...base,
      _m68k_perfetto_init: jest.fn(() => 0),
    };
  }

  return base;
};

const FALLBACK_MODULE_PATH = '../wasm/musashi-node-wrapper.mjs';
const PERF_STUB_SPEC = './__fixtures__/perfetto-stub.mjs';

const importMusashiWrapper = async () => {
  let exportsRef: typeof import('./musashi-wrapper.js') | undefined;
  await jest.isolateModulesAsync(async () => {
    exportsRef = await import('./musashi-wrapper.js');
  });

  if (!exportsRef) {
    throw new Error('Failed to import musashi-wrapper');
  }

  return exportsRef;
};

describe('musashi-wasm Perfetto module selection (Node)', () => {
  afterEach(() => {
    jest.resetModules();
    jest.clearAllMocks();
    delete process.env.MUSASHI_REQUIRE_PERFETTO;
    delete process.env.MUSASHI_FORCE_PERFETTO;
    delete process.env.MUSASHI_DISABLE_PERFETTO;
    delete process.env.MUSASHI_PERFETTO_MODULE;
  });

  it('prefers the Perfetto-enabled module when available', async () => {
    process.env.MUSASHI_PERFETTO_MODULE = PERF_STUB_SPEC;
    const fallbackFactory = jest.fn(async () => createStubModule());

    await jest.unstable_mockModule(FALLBACK_MODULE_PATH, () => ({
      default: fallbackFactory,
    }));

    const { getModule } = await importMusashiWrapper();
    const wrapper = await getModule();

    expect(wrapper.isPerfettoAvailable()).toBe(true);
    expect(fallbackFactory).not.toHaveBeenCalled();
  });

  it('falls back to the standard build when Perfetto load fails', async () => {
    process.env.MUSASHI_PERFETTO_MODULE = PERF_STUB_SPEC;
    const baseModule = createStubModule();
    const fallbackFactory = jest.fn(async () => baseModule);
    const warnSpy = jest.spyOn(console, 'warn').mockImplementation(() => {});

    await jest.unstable_mockModule(PERF_STUB_SPEC, () => ({
      default: async () => {
        throw new Error('missing perf assets');
      },
    }));

    await jest.unstable_mockModule(FALLBACK_MODULE_PATH, () => ({
      default: fallbackFactory,
    }));

    try {
      const { getModule } = await importMusashiWrapper();
      const wrapper = await getModule();
      expect(wrapper.isPerfettoAvailable()).toBe(false);
      expect(fallbackFactory).toHaveBeenCalledTimes(1);
      expect(warnSpy).toHaveBeenCalled();
    } finally {
      warnSpy.mockRestore();
    }
  });

  it('honours MUSASHI_REQUIRE_PERFETTO and surfaces load failures', async () => {
    process.env.MUSASHI_REQUIRE_PERFETTO = '1';
    process.env.MUSASHI_PERFETTO_MODULE = PERF_STUB_SPEC;

    await jest.unstable_mockModule(PERF_STUB_SPEC, () => ({
      default: async () => {
        throw new Error('missing perf assets');
      },
    }));

    await jest.unstable_mockModule(FALLBACK_MODULE_PATH, () => ({
      default: async () => createStubModule(),
    }));

    const { getModule } = await importMusashiWrapper();

    await expect(getModule()).rejects.toThrow(
      /Perfetto-enabled Musashi module failed to load/
    );
  });

  it('skips the Perfetto path when MUSASHI_DISABLE_PERFETTO=1', async () => {
    process.env.MUSASHI_DISABLE_PERFETTO = '1';
    process.env.MUSASHI_PERFETTO_MODULE = PERF_STUB_SPEC;

    const perfSpy = jest.fn();
    const baseModule = createStubModule();
    const fallbackFactory = jest.fn(async () => baseModule);

    await jest.unstable_mockModule(PERF_STUB_SPEC, () => ({
      default: async () => {
        perfSpy();
        return createStubModule({ perfetto: true });
      },
    }));

    await jest.unstable_mockModule(FALLBACK_MODULE_PATH, () => ({
      default: fallbackFactory,
    }));

    const { getModule } = await importMusashiWrapper();
    const wrapper = await getModule();

    expect(wrapper.isPerfettoAvailable()).toBe(false);
    expect(perfSpy).not.toHaveBeenCalled();
    expect(fallbackFactory).toHaveBeenCalledTimes(1);
  });
});
