#!/usr/bin/env node
// Seed a new worktree's firmware/.cache from the canonical checkout without
// sharing mutable build state.
//
// Why not point every checkout at one build_cache_dir: PlatformIO also puts the
// mutable SCons `.sconsign` database there. Sharing it makes concurrent builds
// collide and lets checkouts clobber each other's incremental state.
//
// Instead, each worktree gets its own cache directory inside its Git admin dir
// (`<common>/.git/worktrees/<name>/pio-cache`), and `firmware/.cache` is a
// junction/symlink to it. Object files are hardlinked from the canonical
// checkout; config, CACHEDIR.TAG, and `.sconsign*` stay local to the worktree.
//
// The admin dir is deleted with the worktree, so the cache is cleaned up too.
// `git worktree remove` follows the cache junction, but it can only reach this
// per-worktree cache, never the canonical checkout's cache.
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

function gitValue(args, cwd) {
  return execFileSync('git', args, {
    cwd,
    encoding: 'utf8',
    stdio: ['ignore', 'pipe', 'ignore'],
  }).trim();
}

function mainWorktree() {
  try {
    const out = gitValue(['worktree', 'list', '--porcelain'], ROOT);
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

function lstat(p) {
  try {
    return fs.lstatSync(p);
  } catch (err) {
    if (err.code === 'ENOENT') return null;
    throw err;
  }
}

function exists(p) {
  return lstat(p) !== null;
}

function removeLink(p) {
  try {
    fs.unlinkSync(p);
  } catch (err) {
    // Windows junctions are directories for rmdir but cannot be unlinked.
    if (err.code === 'EPERM' || err.code === 'EISDIR' || err.code === 'EACCES') {
      fs.rmdirSync(p);
      return;
    }
    throw err;
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

function emptyStats(source, target, overrides = {}) {
  return {
    source,
    target,
    dryRun: false,
    linked: 0,
    copied: 0,
    skipped: 0,
    wouldSeed: 0,
    bytes: 0,
    failures: [],
    sourceMissing: false,
    ...overrides,
  };
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
  copyOnly = false,
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
      if (!copyOnly) {
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

function defaultJunction(target, linkPath) {
  fs.symlinkSync(path.resolve(target), linkPath, process.platform === 'win32' ? 'junction' : 'dir');
}

/**
 * Give a linked worktree its own cache in the Git admin directory and point
 * `firmware/.cache` at it. The admin dir is removed with the worktree, so the
 * cache cannot outlive it, and the canonical checkout's cache is read-only.
 */
export function prepareWorktreeCache({
  worktree = process.env.OPENCODE_WORKTREE_PATH || ROOT,
  source = defaultSource(),
  dryRun = false,
  strict = false,
  copyOnly = false,
  link = fs.linkSync,
  copy = fs.copyFileSync,
  junction = defaultJunction,
  git = gitValue,
} = {}) {
  worktree = path.resolve(worktree);
  if (!source) {
    return emptyStats(null, null, { sourceMissing: true });
  }
  source = path.resolve(source);

  const gitDir = path.resolve(worktree, git(['rev-parse', '--git-dir'], worktree));
  const commonDir = path.resolve(worktree, git(['rev-parse', '--git-common-dir'], worktree));
  if (samePath(gitDir, commonDir)) {
    return emptyStats(source, source, { mainCheckout: true, junctioned: false, wouldJunction: false });
  }

  if (!fs.existsSync(source) || !fs.statSync(source).isDirectory()) {
    return emptyStats(source, path.join(gitDir, 'pio-cache'), {
      sourceMissing: true,
      mainCheckout: false,
      junctioned: false,
      wouldJunction: false,
    });
  }

  const cachePath = path.join(worktree, ...CACHE_PARTS);
  const cacheEntry = lstat(cachePath);
  let target;
  let junctioned = false;
  let wouldJunction = false;

  if (cacheEntry?.isSymbolicLink()) {
    target = path.resolve(fs.readlinkSync(cachePath));
    if (samePath(target, source) || isInside(source, target)) {
      // An old whole-cache link into the canonical checkout is unsafe.
      target = path.join(gitDir, 'pio-cache');
      if (dryRun) {
        wouldJunction = true;
      } else {
        removeLink(cachePath);
        fs.mkdirSync(target, { recursive: true });
        junction(target, cachePath);
        junctioned = true;
      }
    }
  } else if (cacheEntry?.isDirectory()) {
    if (fs.readdirSync(cachePath).length === 0) {
      target = path.join(gitDir, 'pio-cache');
      if (dryRun) {
        wouldJunction = true;
      } else {
        fs.rmdirSync(cachePath);
        fs.mkdirSync(target, { recursive: true });
        junction(target, cachePath);
        junctioned = true;
      }
    } else {
      // A cache from an earlier run stays where it is; seed it in place.
      target = cachePath;
    }
  } else if (cacheEntry) {
    throw new Error(`${cachePath} exists and is not a directory`);
  } else {
    target = path.join(gitDir, 'pio-cache');
    if (dryRun) {
      wouldJunction = true;
    } else {
      fs.mkdirSync(target, { recursive: true });
      fs.mkdirSync(path.dirname(cachePath), { recursive: true });
      junction(target, cachePath);
      junctioned = true;
    }
  }

  const stats = seedCache({ source, target, dryRun, strict, copyOnly, link, copy });
  return { ...stats, mainCheckout: false, junctioned, wouldJunction, gitDir };
}

function parseArgs(argv) {
  const options = {
    dryRun: false,
    strict: false,
    copy: false,
    help: false,
    source: null,
    target: null,
  };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === '--help' || arg === '-h') options.help = true;
    else if (arg === '--dry-run') options.dryRun = true;
    else if (arg === '--strict') options.strict = true;
    else if (arg === '--copy') options.copy = true;
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
    'Give this worktree its own PlatformIO cache and hardlink the canonical',
    'checkout\'s immutable objects into it. config and .sconsign* stay local.',
    '',
    '  --source <dir>  cache to read (default: OPENCODE_WORKTREE_BASE/firmware/.cache)',
    '  --target <dir>  seed this cache directly instead of setting up a worktree cache',
    '  --copy          copy objects instead of hardlinking',
    '  --dry-run       print the plan, change nothing',
    '  --strict        exit nonzero if any object cannot be seeded',
    '  --help          show this text',
  ].join('\n');
}

function report(stats) {
  if (stats.mainCheckout) {
    console.log('seed-pio-cache: main checkout keeps its own cache; nothing to seed');
    return 0;
  }
  if (stats.sourceMissing) {
    if (!stats.source) console.log('seed-pio-cache: no canonical checkout found; nothing to seed');
    else console.log(`seed-pio-cache: no source cache at ${stats.source}; nothing to seed`);
    return 0;
  }

  if (stats.wouldJunction) {
    console.log(`seed-pio-cache: would junction ${stats.target} into the worktree cache`);
  } else if (stats.junctioned) {
    console.log(`seed-pio-cache: junctioned ${stats.target} into the worktree cache`);
  }

  const amount = humanBytes(stats.bytes);
  if (stats.dryRun) {
    console.log(`seed-pio-cache: would seed ${stats.wouldSeed} object(s) (${amount}) from ${stats.source}`);
  } else {
    console.log(
      `seed-pio-cache: seeded ${stats.linked + stats.copied} new object(s) ` +
        `(${stats.linked} linked, ${stats.copied} copied, ${stats.skipped} already present; ${amount}) ` +
        `from ${stats.source}`,
    );
  }

  for (const failure of stats.failures) {
    console.warn(`seed-pio-cache: could not seed ${failure.file}: ${failure.code} ${failure.message}`);
  }
  return stats.failures.length > 0 ? 1 : 0;
}

export function run(argv = process.argv.slice(2)) {
  const options = parseArgs(argv);
  if (options.help) {
    console.log(usage());
    return 0;
  }

  if (options.source || options.target) {
    const source = options.source || defaultSource();
    const target = options.target || defaultTarget();
    if (!source) {
      console.log('seed-pio-cache: no canonical checkout found; nothing to seed');
      return 0;
    }
    return report(seedCache({ source, target, dryRun: options.dryRun, strict: options.strict, copyOnly: options.copy }));
  }

  const stats = prepareWorktreeCache({ source: defaultSource(), dryRun: options.dryRun, strict: options.strict, copyOnly: options.copy });
  return report(stats);
}

if (process.argv[1] && samePath(process.argv[1], fileURLToPath(import.meta.url))) {
  try {
    process.exitCode = run();
  } catch (err) {
    console.error(`seed-pio-cache: ${err instanceof Error ? err.message : String(err)}`);
    process.exitCode = 1;
  }
}
