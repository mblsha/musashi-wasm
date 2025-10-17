import { createServer, Server } from 'http';
import { promises as fs } from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

import { expect, test } from '@playwright/test';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const packageRoot = path.resolve(__dirname, '..', '..');

const mimeTypes = new Map<string, string>([
  ['.html', 'text/html; charset=utf-8'],
  ['.js', 'application/javascript; charset=utf-8'],
  ['.mjs', 'application/javascript; charset=utf-8'],
  ['.json', 'application/json; charset=utf-8'],
  ['.map', 'application/json; charset=utf-8'],
  ['.wasm', 'application/wasm']
]);

const resolveContentType = (filePath: string): string => {
  const ext = path.extname(filePath);
  return mimeTypes.get(ext) ?? 'application/octet-stream';
};

test.describe('musashi-wasm browser bundle', () => {
  let server: Server | null = null;
  let baseUrl: string;

  test.beforeAll(async () => {
    server = createServer(async (req, res) => {
      try {
        const requestUrl = new URL(req.url ?? '/', 'http://127.0.0.1');
        let pathname = decodeURIComponent(requestUrl.pathname);
        if (pathname.endsWith('/')) {
          pathname = `${pathname}index.html`;
        }
        const relativePath = pathname.startsWith('/') ? pathname.slice(1) : pathname;
        const filePath = path.resolve(packageRoot, relativePath);
        if (!filePath.startsWith(packageRoot)) {
          res.statusCode = 403;
          res.end('Forbidden');
          return;
        }
        const fileData = await fs.readFile(filePath);
        res.statusCode = 200;
        res.setHeader('Content-Type', resolveContentType(filePath));
        res.end(fileData);
      } catch (error) {
        const err = error as NodeJS.ErrnoException;
        if (err.code === 'ENOENT') {
          res.statusCode = 404;
          res.end('Not found');
        } else {
          res.statusCode = 500;
          res.end('Internal server error');
        }
      }
    });

    await new Promise<void>((resolve) => {
      server!.listen(0, '127.0.0.1', () => resolve());
    });

    const address = server.address();
    if (!address || typeof address === 'string') {
      throw new Error('Failed to start HTTP server');
    }
    baseUrl = `http://127.0.0.1:${address.port}`;
  });

  test.afterAll(async () => {
    if (!server) return;
    await new Promise<void>((resolve) => server!.close(() => resolve()))
      .finally(() => {
        server = null;
      });
  });

  test('createSystem loads in a real browser', async ({ page }) => {
    const errors: Error[] = [];
    page.on('pageerror', (err) => {
      errors.push(err);
    });

    await page.goto(`${baseUrl}/test/browser/index.html`);

    const statusLocator = page.locator('#status');
    await expect(statusLocator).toHaveText('createSystem: ok', { timeout: 30_000 });

    expect(errors).toEqual([]);
  });

  test('createSystem exposes Perfetto tracing in Node-like DOM environments', async () => {
    const envBackup = { ...process.env };
    const originalWindow = (globalThis as { window?: unknown }).window;
    const originalDocument = (globalThis as { document?: unknown }).document;
    const originalNavigator = (globalThis as { navigator?: unknown }).navigator;

    try {
      process.env.MUSASHI_REQUIRE_PERFETTO = '1';
      process.env.MUSASHI_PERFETTO_MODULE = '../../perf.mjs';
      delete process.env.MUSASHI_DISABLE_PERFETTO;
      delete process.env.MUSASHI_RUNTIME;

      (globalThis as { window: unknown }).window = {};
      (globalThis as { document: unknown }).document = {};
      (globalThis as { navigator: { userAgent: string } }).navigator = {
        userAgent: 'happy-dom',
      };

      const { createSystem } = await import('../../lib/core/index.js');

      const rom = new Uint8Array(0x2000);
      // Reset SP = 0x00100000, PC = 0x00000400
      rom.set([0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00], 0x0);
      // Simple program at 0x400: NOP; NOP; RTS
      rom.set([0x4e, 0x71, 0x4e, 0x71, 0x4e, 0x75], 0x400);

      const system = await createSystem({
        rom,
        ramSize: 0x2000,
      });

      expect(system.tracer.isAvailable()).toBe(true);

      system.tracer.start({ instructions: true });
      system.run(4);
      const trace = system.tracer.stop();

      expect(trace).toBeInstanceOf(Uint8Array);
      expect(trace.length).toBeGreaterThan(0);

      system.cleanup();
    } finally {
      for (const key of Object.keys(process.env)) {
        if (!(key in envBackup)) {
          delete process.env[key];
        }
      }
      for (const [key, value] of Object.entries(envBackup)) {
        if (value !== undefined) {
          process.env[key] = value;
        }
      }

      if (originalWindow === undefined) {
        Reflect.deleteProperty(globalThis, 'window');
      } else {
        (globalThis as { window: unknown }).window = originalWindow;
      }

      if (originalDocument === undefined) {
        Reflect.deleteProperty(globalThis, 'document');
      } else {
        (globalThis as { document: unknown }).document = originalDocument;
      }

      if (originalNavigator === undefined) {
        Reflect.deleteProperty(globalThis, 'navigator');
      } else {
        (globalThis as { navigator: unknown }).navigator = originalNavigator;
      }
    }
  });

  test('Perfetto tracing works in a real browser without env overrides', async ({ page }) => {
    await page.goto(`${baseUrl}/test/browser/index.html`);

    const traceLength = await page.evaluate(async () => {
      const { createSystem } = await import('../../lib/core/index.js');

      const rom = new Uint8Array(0x2000);
      rom.set([0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00], 0x0);
      rom.set([0x4e, 0x71, 0x4e, 0x71, 0x4e, 0x75], 0x400);

      const system = await createSystem({ rom, ramSize: 0x2000 });
      try {
        if (!system.tracer.isAvailable()) {
          throw new Error('Perfetto tracer not available in browser runtime');
        }
        system.tracer.start({ instructions: true });
        system.run(4);
        const trace = system.tracer.stop();
        if (!(trace instanceof Uint8Array)) {
          throw new Error('Trace result is not a Uint8Array');
        }
        return trace.length;
      } finally {
        system.cleanup();
      }
    });

    expect(traceLength).toBeGreaterThan(0);
  });
});
