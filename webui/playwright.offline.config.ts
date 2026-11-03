import { defineConfig } from '@playwright/test';

/**
 * Playwright config for the offline tier.
 *
 * Not part of `npm test` (which is plain `node --test`) and not run by CI.
 * It needs a browser and a built `dist/`, so it stays a separate script.
 *
 * The app is served from `http://localhost`, which counts as a secure
 * context, so the service worker registers and the cache behaviour can be
 * exercised for real.
 */
export default defineConfig({
  testDir: './tests/e2e',
  testMatch: '**/offline-sw.spec.ts',
  fullyParallel: false,
  workers: 1,
  retries: 0,
  forbidOnly: !!process.env.CI,
  timeout: 120_000,
  expect: { timeout: 20_000 },
  reporter: [['list']],
  use: {
    baseURL: 'http://localhost:4173',
  },
  webServer: {
    command: 'npm run preview -- --port 4173 --strictPort',
    url: 'http://localhost:4173',
    reuseExistingServer: !process.env.CI,
    timeout: 60_000,
  },
});
