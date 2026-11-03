import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { run, samePath, seedCache } from './seed-pio-cache.mjs';

function tempDir(t) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'seed-pio-cache-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  return dir;
}

function makeCache(root) {
  const cache = path.join(root, 'firmware', '.cache');
  fs.mkdirSync(cache, { recursive: true });
  return cache;
}

function put(cache, relative, content) {
  const file = path.join(cache, relative);
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, content);
  return file;
}

test('links only two-hex object files and leaves metadata to the target checkout', (t) => {
  const root = tempDir(t);
  const source = makeCache(path.join(root, 'main'));
  put(source, '00/aaa.o', 'one');
  put(source, 'A3/bbb.o', 'two');
  put(source, 'config', 'prefix=2');
  put(source, 'CACHEDIR.TAG', 'tag');
  put(source, '.sconsign313.dblite', 'state');
  put(source, 'parse_check/scratch.txt', 'scratch');
  const target = path.join(root, 'worktree', 'firmware', '.cache');

  const stats = seedCache({ source, target });

  assert.equal(stats.linked, 2);
  assert.equal(stats.copied, 0);
  assert.equal(stats.skipped, 0);
  assert.deepEqual(stats.failures, []);
  assert.equal(fs.readFileSync(path.join(target, '00', 'aaa.o'), 'utf8'), 'one');
  assert.equal(fs.readFileSync(path.join(target, 'A3', 'bbb.o'), 'utf8'), 'two');
  assert.ok(fs.statSync(path.join(target, '00', 'aaa.o')).nlink >= 2, 'object should be a hardlink');

  for (const relative of ['config', 'CACHEDIR.TAG', '.sconsign313.dblite', 'parse_check']) {
    assert.equal(fs.existsSync(path.join(target, relative)), false, `${relative} must stay local`);
  }
});

test('is idempotent and never overwrites an existing target object', (t) => {
  const root = tempDir(t);
  const source = makeCache(path.join(root, 'main'));
  put(source, '00/aaa.o', 'one');
  const target = path.join(root, 'worktree', 'firmware', '.cache');

  assert.equal(seedCache({ source, target }).linked, 1);
  const inode = fs.statSync(path.join(target, '00', 'aaa.o')).ino;

  const second = seedCache({ source, target });
  assert.equal(second.linked, 0);
  assert.equal(second.skipped, 1);
  assert.equal(fs.statSync(path.join(target, '00', 'aaa.o')).ino, inode);

  put(target, '00/aaa.o', 'keep');
  const third = seedCache({ source, target });
  assert.equal(third.skipped, 1);
  assert.equal(fs.readFileSync(path.join(target, '00', 'aaa.o'), 'utf8'), 'keep');
});

test('dry-run reports objects without creating the target cache', (t) => {
  const root = tempDir(t);
  const source = makeCache(path.join(root, 'main'));
  put(source, '00/aaa.o', 'one');
  put(source, '01/bbb.o', 'two');
  const target = path.join(root, 'worktree', 'firmware', '.cache');

  const stats = seedCache({ source, target, dryRun: true });

  assert.equal(stats.wouldSeed, 2);
  assert.equal(stats.linked, 0);
  assert.equal(fs.existsSync(target), false);
});

test('refuses to seed a cache into itself or its own subtree', (t) => {
  const root = tempDir(t);
  const source = makeCache(path.join(root, 'main'));
  put(source, '00/aaa.o', 'one');

  assert.throws(() => seedCache({ source, target: source }), /separate trees/);
  assert.throws(() => seedCache({ source, target: path.join(source, '00', 'nested') }), /separate trees/);
  assert.throws(() => seedCache({ source, target: path.dirname(source) }), /separate trees/);
});

test('falls back to a copy when hardlinking is unavailable', (t) => {
  const root = tempDir(t);
  const source = makeCache(path.join(root, 'main'));
  put(source, '00/aaa.o', 'one');
  put(source, '01/bbb.o', 'two');
  const target = path.join(root, 'worktree', 'firmware', '.cache');
  let copies = 0;

  const stats = seedCache({
    source,
    target,
    link: () => {
      const err = new Error('cross-device link');
      err.code = 'EXDEV';
      throw err;
    },
    copy: (from, to, flags) => {
      copies++;
      fs.copyFileSync(from, to, flags);
    },
  });

  assert.equal(stats.linked, 0);
  assert.equal(stats.copied, 2);
  assert.equal(copies, 2);
  assert.deepEqual(stats.failures, []);
  assert.equal(fs.readFileSync(path.join(target, '00', 'aaa.o'), 'utf8'), 'one');
});

test('a missing source cache is a no-op, not an error', (t) => {
  const root = tempDir(t);
  const target = path.join(root, 'worktree', 'firmware', '.cache');

  const stats = seedCache({ source: path.join(root, 'missing'), target });
  assert.equal(stats.sourceMissing, true);
  assert.equal(fs.existsSync(target), false);

  assert.equal(run(['--source', path.join(root, 'missing'), '--target', target]), 0);
});

test('reports failures, and --strict turns them into an error', (t) => {
  const root = tempDir(t);
  const source = makeCache(path.join(root, 'main'));
  put(source, '00/aaa.o', 'one');
  const target = path.join(root, 'worktree', 'firmware', '.cache');
  const link = () => {
    const err = new Error('disk error');
    err.code = 'EIO';
    throw err;
  };

  const stats = seedCache({ source, target, link });
  assert.equal(stats.linked, 0);
  assert.equal(stats.failures.length, 1);
  assert.equal(stats.failures[0].code, 'EIO');

  const strictTarget = path.join(root, 'worktree2', 'firmware', '.cache');
  assert.throws(() => seedCache({ source, target: strictTarget, link, strict: true }), /could not be seeded/);
});

test('samePath is case-insensitive only where the filesystem is', () => {
  const a = path.join(os.tmpdir(), 'Seed-Cache-Path');
  const b = path.join(os.tmpdir(), 'seed-cache-path');
  assert.equal(samePath(a, b), process.platform === 'win32');
});
