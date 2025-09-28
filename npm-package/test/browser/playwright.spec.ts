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
});
