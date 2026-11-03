#!/usr/bin/env node
// Seed firmware/.cache in a new worktree with hardlinks to the canonical
// checkout's immutable SCons cache objects.
//
// Why not share `build_cache_dir`: PlatformIO also puts the mutable SCons
// `.sconsign` database in that directory. Sharing it makes concurrent builds
// collide and lets checkouts clobber each other's incremental state.
//
// Hardlinking only the two-hex object directories keeps the object data
// deduplicated while every checkout keeps its own `config` and `.sconsign*`.
// SCons cache objects are immutable: a build only adds new files, it never
// rewrites an existing cache entry.
//
// OpenCode runs this as the project's worktree startup script and sets:
//   OPENCODE_WORKTREE_BASE  canonical checkout
//   OPENCODE_WORKTREE_PATH  new worktree
//
// Manual use:
//   node tools/seed-pio-cache.mjs --dry-run
//   node tools/seed-pio-cache.mjs --source <cache> --target <cache>
//   node tools/seed-pio-cache.mjs --help

import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const CACHE_PARTS = ['firmware', '.cache'];
const OBJECT_DIR = /^[0-9a-f]{2}$/i;
const COPY_FALLBACK = new Set(['EXDEV', 'EPERM', 'EACCES', 'ENOTSUP', 'EMLINK', 'EINVAL']);

/** Case-insensitive on Windows, exact elsewhere. */
export function samePath(a, b) {
  const left = path.resolve(a);
  const right = path.resolve(b);
  return process.platform === 'win32' ? left.toLowerCase() === right.toLowerCase() : left === right;
}

function isInside(parent, child) {
  const rel = path.relative(parent, child);
  return rel !== '' && !rel.startsWith('..') && !path.isAbsolute(rel);
}

function mainWorktree() {
  try {
    const out = execFileSync('git', ['worktree', 'list', '--porcelain'], {
      cwd: ROOT,
      encoding: 'utf8',
      stdio: ['ignore', 'pipe', 'ignore'],
    });
    const line = out.split(/\r?\n/).find((entry) => entry.startsWith('worktree '));
    return line ? path.resolve(line.slice('worktree '.length).trim()) : null;
  } catch {
    return null;
  }
}

function defaultSource() {
  const base = process.env.OPENCODE_WORKTREE_BASE || mainWorktree();
  return base ? path.join(base, ...CACHE_PARTS) : null;
}

function defaultTarget() {
  return path.join(process.env.OPENCODE_WORKTREE_PATH || ROOT, ...CACHE_PARTS);
}

function exists(p) {
  try {
    fs.lstatSync(p);
    return true;
  } catch {
    return false;
  }
}

function humanBytes(bytes) {
  if (bytes < 1024) return `${bytes} B`;
  const units = ['KB', 'MB', 'GB', 'TB'];
  let value = bytes / 1024;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit++;
  }
  return `${value.toFixed(1)} ${units[unit]}`;
}

/**
 * Hardlink every regular object file from `source` into `target`.
 * Existing target files are never overwritten; root metadata such as `config`,
 * `CACHEDIR.TAG`, and `.sconsign*` is deliberately left to the new checkout.
 */
export function seedCache({
  source,
  target,
  dryRun = false,
  strict = false,
  link = fs.linkSync,
  copy = fs.copyFileSync,
}) {
  source = path.resolve(source);
  target = path.resolve(target);

  if (samePath(source, target) || isInside(source, target) || isInside(target, source)) {
    throw new Error(`source and target must be separate trees (source ${source}, target ${target})`);
  }

  const stats = {
    source,
    target,
    dryRun,
    linked: 0,
    copied: 0,
    skipped: 0,
    wouldSeed: 0,
    bytes: 0,
    failures: [],
    sourceMissing: false,
  };

  if (!fs.existsSync(source) || !fs.statSync(source).isDirectory()) {
    stats.sourceMissing = true;
    return stats;
  }

  for (const dirent of fs.readdirSync(source, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
    if (!dirent.isDirectory() || !OBJECT_DIR.test(dirent.name)) continue;

    const sourceDir = path.join(source, dirent.name);
    const targetDir = path.join(target, dirent.name);

    for (const name of fs.readdirSync(sourceDir).sort()) {
      const from = path.join(sourceDir, name);
      const entry = fs.lstatSync(from);
      if (!entry.isFile()) continue;

      const to = path.join(targetDir, name);
      if (exists(to)) {
        stats.skipped++;
        continue;
      }

      stats.bytes += entry.size;
      if (dryRun) {
        stats.wouldSeed++;
        continue;
      }

      fs.mkdirSync(targetDir, { recursive: true });
      try {
        link(from, to);
        stats.linked++;
        continue;
      } catch (err) {
        if (err.code === 'EEXIST') {
          stats.skipped++;
          stats.bytes -= entry.size;
          continue;
        }
        if (!COPY_FALLBACK.has(err.code)) {
          stats.failures.push({ file: to, code: err.code, message: err.message });
          continue;
        }
      }

      try {
        copy(from, to, fs.constants.COPYFILE_EXCL);
        stats.copied++;
      } catch (err) {
        if (err.code === 'EEXIST') {
          stats.skipped++;
          stats.bytes -= entry.size;
        } else {
          stats.failures.push({ file: to, code: err.code, message: err.message });
        }
      }
    }
  }

  if (strict && stats.failures.length > 0) {
    const first = stats.failures[0];
    throw new Error(`${stats.failures.length} object(s) could not be seeded; first: ${first.file} (${first.code})`);
  }
  return stats;
}

function parseArgs(argv) {
  const options = { dryRun: false, strict: false, help: false, source: null, target: null };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === '--help' || arg === '-h') options.help = true;
    else if (arg === '--dry-run') options.dryRun = true;
    else if (arg === '--strict') options.strict = true;
    else if (arg === '--source') options.source = argv[++i];
    else if (arg.startsWith('--source=')) options.source = arg.slice('--source='.length);
    else if (arg === '--target') options.target = argv[++i];
    else if (arg.startsWith('--target=')) options.target = arg.slice('--target='.length);
    else throw new Error(`unknown argument: ${arg}`);
  }
  return options;
}

function usage() {
  return [
    'Usage: node tools/seed-pio-cache.mjs [options]',
    '',
    'Hardlink immutable PlatformIO SCons cache objects from the canonical',
    'checkout into this worktree. Mutable metadata stays per checkout.',
    '',
    '  --source <dir>  cache to read (default: OPENCODE_WORKTREE_BASE/firmware/.cache)',
    '  --target <dir>  cache to seed (default: OPENCODE_WORKTREE_PATH/firmware/.cache)',
    '  --dry-run       print the plan, change nothing',
    '  --strict        exit nonzero if any object cannot be seeded',
    '  --help          show this text',
  ].join('\n');
}

export function run(argv = process.argv.slice(2)) {
  const options = parseArgs(argv);
  if (options.help) {
    console.log(usage());
    return 0;
  }

  const source = options.source || defaultSource();
  const target = options.target || defaultTarget();
  if (!source) {
    console.log('seed-pio-cache: no canonical checkout found; nothing to seed');
    return 0;
  }

  const stats = seedCache({ source, target, dryRun: options.dryRun, strict: options.strict });

  if (stats.sourceMissing) {
    console.log(`seed-pio-cache: no source cache at ${stats.source}; nothing to seed`);
    return 0;
  }

  const amount = humanBytes(stats.bytes);
  if (stats.dryRun) {
    console.log(`seed-pio-cache: would seed ${stats.wouldSeed} object(s) (${amount}) from ${stats.source}`);
  } else {
    console.log(
      `seed-pio-cache: seeded ${stats.linked + stats.copied} new object(s) ` +
        `(${stats.linked} linked, ${stats.copied} copied, ${stats.skipped} already present; ${amount} deduplicated) ` +
        `from ${stats.source}`,
    );
  }

  for (const failure of stats.failures) {
    console.warn(`seed-pio-cache: could not seed ${failure.file}: ${failure.code} ${failure.message}`);
  }
  return stats.failures.length > 0 ? 1 : 0;
}

if (process.argv[1] && samePath(process.argv[1], fileURLToPath(import.meta.url))) {
  try {
    process.exitCode = run();
  } catch (err) {
    console.error(`seed-pio-cache: ${err instanceof Error ? err.message : String(err)}`);
    process.exitCode = 1;
  }
}
