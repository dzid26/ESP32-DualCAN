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
 *
 * Two installs are covered:
 * - root: the app at the origin root, the way the device serves it
 * - subpath: the app under a path, the way GitHub Pages serves it
 *
 * The sub-path case is the one that matters. A worker registered or
 * precached against the origin root never works there, and it fails
 * quietly: the registration 404s, the install is discarded, and the app
 * simply has no worker.
 */
const ROOT = 'http://localhost:4173/';
const SUBPATH = 'http://localhost:4174/ESP32-DualCAN/';

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
  projects: [
    { name: 'root', use: { baseURL: ROOT } },
    { name: 'subpath', use: { baseURL: SUBPATH } },
  ],
  webServer: [
    {
      command: 'npm run preview -- --port 4173 --strictPort',
      url: ROOT,
      reuseExistingServer: !process.env.CI,
      timeout: 60_000,
    },
    {
      command:
        'npm run preview -- --base /ESP32-DualCAN/ --outDir dist-sub --port 4174 --strictPort',
      url: SUBPATH,
      reuseExistingServer: !process.env.CI,
      timeout: 60_000,
    },
  ],
});
