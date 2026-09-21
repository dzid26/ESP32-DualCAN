/**
 * ble-real-device.spec.ts — real-hardware tier, ON by default for developers.
 *
 * ============================== READ FIRST ==============================
 * Selecting a device in the native chooser is an OVER-THE-AIR action against a
 * physical peripheral, so this spec is skipped automatically in automated
 * contexts and runs only when a developer invokes it directly:
 *
 *   cd webui
 *   npm run test:ble:real:install  # once per machine: downloads Playwright's Chromium
 *   npm run test:ble:real          # headful; opens a visible Chromium window
 *
 * Not collected by `npm test` (which globs tests/*.test.ts) and never run by CI.
 *
 * It is skipped (before any browser launches) when EITHER holds:
 *   - BLE_SKIP_REAL_DEVICE is truthy (set it to '1' to force-skip), OR
 *   - CI is truthy (GitHub Actions / Travis / GitLab / CircleCI all set CI).
 *
 * Env vars:
 *   BLE_SKIP_REAL_DEVICE  truthy -> force-skip (browser never launches)
 *   CI                    truthy -> skip (automatic in CI)
 *   BLE_REAL_MATCH        advertised-name prefix to authorize (default 'dorky')
 *   BLE_REAL_SCAN_MS      chooser scan timeout before a no-hardware skip (default 30000)
 *   BLE_REAL_REBOOT_MS    wait before the simulated-reboot reconnect (default 5000)
 *
 * If the browser opens but no device matching `dorky` appears, the test
 * reports SKIPPED rather than failing — a developer without a powered device is
 * not blocked. A hard failure is reserved for genuine assertions once a device
 * has actually been selected/connected.
 *
 * A headful browser is MANDATORY (the native chooser cannot complete headless);
 * `playwright.ble.config.ts` sets `headless: false`.
 *
 * Guardrails:
 *   - Automated context -> the whole describe is skipped before any browser/radio is touched.
 *   - Only the explicitly authorized name prefix is selected. Never falls back.
 *   - Ambiguous match -> stop and list candidates; select nothing.
 *   - No matching device -> cancel the prompt and report skipped.
 *   - Never pairs, unpairs, or mutates OS Bluetooth / adapter state.
 * =========================================================================
 */

import http from 'node:http';
import type { AddressInfo } from 'node:net';
import { test, expect, type Page } from '@playwright/test';
import {
  BleChooserError,
  BleChooserErrorCode,
  connectGatt,
  disconnectGatt,
  enableDeviceAccess,
  installRequestDeviceHarness,
  reconnectSavedDevice,
  triggerRequestDevice,
  waitForRequestDeviceResult,
  type BleChooser,
} from './ble-chooser';

/** Treat common truthy env spellings ('1', 'true', 'yes', 'on') as enabled. */
function envTruthy(value: string | undefined): boolean {
  if (value == null) return false;
  const v = value.trim().toLowerCase();
  return v !== '' && v !== '0' && v !== 'false' && v !== 'no' && v !== 'off';
}

/** Skip in automation; a developer run has neither CI nor BLE_SKIP_REAL_DEVICE set. */
const SKIP_REAL_DEVICE = envTruthy(process.env.BLE_SKIP_REAL_DEVICE) || envTruthy(process.env.CI);
/** Case-insensitive advertised-name prefix. Firmware advertises "Dorky-XXXX". */
const MATCH = process.env.BLE_REAL_MATCH ?? 'dorky';
const SCAN_MS = Number(process.env.BLE_REAL_SCAN_MS ?? 30_000);
/** Nordic UART Service — the firmware's primary service. */
const NUS_SERVICE = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';

/** Serve a minimal localhost page so the context is a secure origin. */
function startLocalServer(): Promise<{ server: http.Server; origin: string }> {
  return new Promise((resolve) => {
    const server = http.createServer((_req, res) => {
      res.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
      res.end('<!doctype html><meta charset="utf-8"><title>Dorky BLE real-device harness</title><body></body>');
    });
    server.listen(0, '127.0.0.1', () => {
      const address = server.address() as AddressInfo;
      resolve({ server, origin: `http://127.0.0.1:${address.port}/` });
    });
  });
}

/** Run `body` with a fresh localhost origin; always close the server. */
async function withLocalPage(page: Page, body: (chooser: BleChooser) => Promise<void>): Promise<void> {
  const { server, origin } = await startLocalServer();
  try {
    // DeviceAccess must be enabled BEFORE requestDevice() is triggered.
    const chooser = await enableDeviceAccess(page);
    await page.goto(origin, { waitUntil: 'domcontentloaded', timeout: 15_000 });
    expect(await page.evaluate(() => !!navigator.bluetooth)).toBe(true);
    await body(chooser);
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
  }
}

/** Open the native chooser, and select ONLY the authorized-name device. */
async function selectDeviceViaChooser(page: Page, chooser: BleChooser): Promise<void> {
  await installRequestDeviceHarness(page, { acceptAllDevices: true, optionalServices: [NUS_SERVICE] });
  await triggerRequestDevice(page);
  let device;
  try {
    device = await chooser.waitForDevice({ prefix: MATCH }, { timeoutMs: SCAN_MS });
  } catch (error) {
    if (error instanceof BleChooserError && error.code === BleChooserErrorCode.NOT_FOUND) {
      // No powered/in-range peripheral. This is not a test failure — report the
      // run as skipped so a developer without hardware is not blocked.
      await chooser.cancelPrompt().catch(() => false);
      const candidates = (error.candidates ?? []).map((d) => ({ id: d.id, name: d.name }));
      test.skip(
        true,
        `No "${MATCH}" device found within ${SCAN_MS}ms (candidates: ${JSON.stringify(candidates)}); ` +
          'skipping the real-device tier.',
      );
      return;
    }
    if (error instanceof BleChooserError) {
      await chooser.cancelPrompt().catch(() => false);
      const candidates = (error.candidates ?? error.matches ?? []).map((d) => ({ id: d.id, name: d.name }));
      throw new Error(`Refusing to select (${error.code}). Candidates: ${JSON.stringify(candidates)}`);
    }
    throw error;
  }
  await chooser.selectDevice(device);
  const resolved = await waitForRequestDeviceResult(page, { timeoutMs: 20_000 });
  expect(resolved.name).toBe(device.name);

  const gatt = await connectGatt(page, { timeoutMs: 20_000 });
  expect(gatt.connected, 'GATT connect failed').toBe(true);
}

test.describe('real device', () => {
  test.skip(
    SKIP_REAL_DEVICE,
    'Real-hardware tier skipped in an automated context (CI or BLE_SKIP_REAL_DEVICE is set). ' +
      'Unset it (or set BLE_SKIP_REAL_DEVICE=0) and run `npm run test:ble:real` against hardware. ' +
      'No browser was launched and no radio was touched.',
  );

  test('silent reconnect after drop', async ({ page }) => {
    await withLocalPage(page, async (chooser) => {
      await selectDeviceViaChooser(page, chooser);

      // Simulate an unexpected drop.
      await disconnectGatt(page);

      // Silent auto-reconnect: getDevices() + gatt.connect() with NO chooser.
      const reconnected = await reconnectSavedDevice(page, MATCH, 20_000);
      expect(reconnected.supported, 'navigator.bluetooth.getDevices() must be available').toBe(true);
      expect(
        reconnected.found,
        `no previously-permitted device matching "${MATCH}" (saw: ${JSON.stringify(reconnected.candidates)})`,
      ).toBe(true);
      expect(reconnected.connected, 'silent reconnect failed').toBe(true);

      await disconnectGatt(page);
    });
  });

  test('reconnect after reboot delay', async ({ page }) => {
    await withLocalPage(page, async (chooser) => {
      await selectDeviceViaChooser(page, chooser);
      await disconnectGatt(page);

      // Wait long enough for a rebooting peripheral to re-advertise.
      await page.waitForTimeout(Number(process.env.BLE_REAL_REBOOT_MS ?? 5_000));

      const reconnected = await reconnectSavedDevice(page, MATCH, 20_000);
      expect(reconnected.supported).toBe(true);
      expect(
        reconnected.found,
        `device not rediscovered after reboot (saw: ${JSON.stringify(reconnected.candidates)})`,
      ).toBe(true);
      expect(reconnected.connected, 'reconnect after simulated reboot failed').toBe(true);

      await disconnectGatt(page);
    });
  });
});
