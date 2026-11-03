import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

/**
 * GitHub Pages serves this app under a path, `/<repo>/`, so a request that
 * starts at the origin root reaches somebody else's site or a 404. Both the
 * worker registration and the scripting guide got this wrong and failed only
 * on the deploy, where nothing local could catch it.
 *
 * Every runtime request therefore has to go through `import.meta.env.BASE_URL`.
 * This test fails with the offending files rather than leaving the next one to
 * be found on a phone.
 */

const __dirname = dirname(fileURLToPath(import.meta.url));
const SRC = join(__dirname, '..', 'src');
const WEBUI = join(__dirname, '..');

function sources(dir: string): string[] {
  const found: string[] = [];
  for (const entry of readdirSync(dir)) {
    const full = join(dir, entry);
    if (statSync(full).isDirectory()) found.push(...sources(full));
    else if (/\.(ts|svelte)$/.test(entry)) found.push(full);
  }
  return found;
}

const ORIGIN_ROOT_FETCH = /fetch\(\s*['"`]\//;

test('no source file fetches from the origin root', () => {
  const offenders = sources(SRC)
    .filter((file) => ORIGIN_ROOT_FETCH.test(readFileSync(file, 'utf8')))
    .map((file) => relative(WEBUI, file).replace(/\\/g, '/'));

  assert.deepEqual(
    offenders,
    [],
    'these files fetch from the origin root and break under the deploy base. ' +
      'Use a BASE_URL-prefixed path instead: ' +
      offenders.join(', '),
  );
});
