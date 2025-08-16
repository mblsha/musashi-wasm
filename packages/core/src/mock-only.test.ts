// Smoke test to confirm mock isolation works correctly
import { jest } from '@jest/globals';

let getModule: () => Promise<any>;

beforeAll(async () => {
  // Mock the wrapper module before any imports
  await jest.unstable_mockModule('./musashi-wrapper.js', async () => {
    return await import('./__mocks__/musashi-wrapper.js');
  });

  // Import the wrapper after mocking (which should give us the mock)
  ({ getModule } = await import('./musashi-wrapper.js'));
});

test('mock wrapper loads and instantiates', async () => {
  const w = await getModule();
  expect(w).toBeDefined();
  expect(w.isPerfettoAvailable).toBeDefined();
  expect(w.isPerfettoAvailable()).toBe(false); // Mock always returns false
});

test('mock wrapper has expected methods', async () => {
  const w = await getModule();
  
  // Check core methods exist
  expect(typeof w.init).toBe('function');
  expect(typeof w.reset).toBe('function');
  expect(typeof w.execute).toBe('function');
  expect(typeof w.get_reg).toBe('function');
  expect(typeof w.set_reg).toBe('function');
  expect(typeof w.read_memory).toBe('function');
  expect(typeof w.write_memory).toBe('function');
  
  // Check Perfetto methods exist (as stubs)
  expect(typeof w.perfettoInit).toBe('function');
  expect(typeof w.perfettoDestroy).toBe('function');
  expect(typeof w.perfettoEnableFlow).toBe('function');
  expect(typeof w.perfettoEnableMemory).toBe('function');
  expect(typeof w.perfettoEnableInstructions).toBe('function');
  expect(typeof w.perfettoExportTrace).toBe('function');
});