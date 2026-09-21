/**
 * Automatic Chromium provisioning for the real-hardware BLE tier.
 *
 * Runs before the suite: if Playwright's Chromium executable is absent and the
 * run is not in an automated/skipped context, downloads it once with
 * `playwright install chromium`. Present browsers are never re-verified or
 * re-downloaded, so a normal run is a single `existsSync` check.
 */

import { execSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { chromium } from '@playwright/test';

/** Treat common truthy env spellings ('1', 'true', 'yes', 'on') as enabled. */
function envTruthy(value: string | undefined): boolean {
  if (value == null) return false;
  const v = value.trim().toLowerCase();
  return v !== '' && v !== '0' && v !== 'false' && v !== 'no' && v !== 'off';
}

export default function globalSetup(): void {
  // Match the spec's skip contract: an automated context downloads nothing.
  if (envTruthy(process.env.BLE_SKIP_REAL_DEVICE) || envTruthy(process.env.CI)) return;

  const executablePath = chromium.executablePath();
  if (existsSync(executablePath)) return;

  console.log(`[ble-real] Playwright Chromium missing at ${executablePath}; running "npx playwright install chromium" (one time).`);
  execSync('npx playwright install chromium', { stdio: 'inherit' });

  if (!existsSync(executablePath)) {
    throw new Error(`[ble-real] "npx playwright install chromium" finished but ${executablePath} is still missing.`);
  }
}
