import { defineConfig } from '@playwright/test';

/**
 * Playwright config for the real-hardware BLE tier (default on for developers).
 *
 * Not part of `npm test` (which is plain `node --test`) and never invoked by CI.
 * `headless: false` is MANDATORY — the native Web Bluetooth chooser cannot
 * complete headless. The spec skips itself when `CI` or `BLE_SKIP_REAL_DEVICE`
 * is truthy, so an automated run launches no browser and touches no radio.
 *
 * `globalSetup` downloads Playwright's Chromium automatically (once) when it is
 * missing, unless the run is in that same automated/skipped context. The
 * explicit `npm run test:ble:real:install` script remains available.
 */
export default defineConfig({
  testDir: './tests/e2e',
  globalSetup: './playwright.ble.global-setup.ts',
  fullyParallel: false,
  workers: 1,
  retries: 0,
  forbidOnly: !!process.env.CI,
  timeout: 120_000,
  expect: { timeout: 20_000 },
  reporter: [['list']],
  // Short project name so list output stays readable.
  projects: [
    {
      name: 'ble-real',
      use: {
        // Required for the native chooser; the spec self-skips before launch in automation.
        headless: false,
        launchOptions: {
          // WebBluetooth: native chooser. WebBluetoothNewPermissionsBackend:
          // required for navigator.bluetooth.getDevices() (the silent-reconnect
          // path) — without it getDevices is undefined on Chromium 153.
          args: ['--enable-features=WebBluetooth,WebBluetoothNewPermissionsBackend'],
        },
      },
    },
  ],
});
