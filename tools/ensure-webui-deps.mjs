#!/usr/bin/env node
// Runs before check/dev/host/build. A fresh git worktree has no
// webui/node_modules (gitignored, not shared); the mocked test tier needs none,
// but svelte-check and Vite do.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const webui = fileURLToPath(new URL('../webui/', import.meta.url));
if (fs.existsSync(path.join(webui, 'node_modules'))) process.exit(0);

console.log('ensure-webui-deps: webui/node_modules is missing, running npm ci');
// npm exports its config to lifecycle scripts as npm_config_*. A user-level
// `allow-scripts` setting is rejected as a flag by a project-scoped install, so
// drop it before nesting npm.
const env = Object.fromEntries(
  Object.entries(process.env).filter(([key]) => key.toLowerCase() !== 'npm_config_allow_scripts'),
);
execFileSync(process.platform === 'win32' ? 'npm.cmd' : 'npm', ['ci', '--no-audit', '--no-fund'], {
  cwd: webui,
  stdio: 'inherit',
  shell: process.platform === 'win32',
  env,
});
