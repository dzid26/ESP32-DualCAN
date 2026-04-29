import { defineConfig } from '@playwright/test';
import { existsSync } from 'fs';

const CHROME_CANDIDATES = [
  '/home/dzid/.nix-profile/bin/google-chrome',
  '/usr/bin/google-chrome',
  '/usr/bin/chromium-browser',
  '/usr/bin/chromium',
];
const CHROME = CHROME_CANDIDATES.find(p => existsSync(p)) ?? '';

export default defineConfig({
  testDir: './tests',
  fullyParallel: true,
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 1 : 0,
  reporter: 'list',
  use: {
    baseURL: 'http://localhost:5173',
    ...(CHROME
      ? { launchOptions: { executablePath: CHROME, args: ['--no-sandbox', '--disable-dev-shm-usage'] } }
      : {}),
  },
  projects: [
    { name: 'chromium' },
  ],
  webServer: {
    command: 'npm run dev',
    url: 'http://localhost:5173',
    reuseExistingServer: !process.env.CI,
  },
});
