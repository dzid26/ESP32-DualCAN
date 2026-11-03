/**
 * Compare the current preprocessor with a historical implementation.
 *
 *   node webui/scripts/diff-preprocessor.ts [--old <git-rev>]
 *
 * Defaults to bc24000, the last pre-lexer implementation on main. Exits 1 on
 * any unexplained output, error-count, or warning-count difference.
 */
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { parseDbc, type Message } from '../src/dbc/parser.ts';
import { preprocessScript as newPreprocess } from '../src/lib/preprocessor.ts';

interface OldResult {
  code: string;
  errors: string[];
  warnings: string[];
}

type OldPreprocess = (source: string, messages: Message[]) => OldResult;

const here = dirname(fileURLToPath(import.meta.url));
const repo = join(here, '..', '..');
const oldIndex = process.argv.indexOf('--old');
const oldRev = oldIndex >= 0 ? process.argv[oldIndex + 1] : 'bc24000';

// Node refuses type stripping inside node_modules, so extract the old source
// into an OS temp directory instead.
const tempDir = mkdtempSync(join(tmpdir(), 'diff-preprocessor-'));
const oldPath = join(tempDir, 'preprocessor-old.mts');
writeFileSync(
  oldPath,
  execFileSync('git', ['show', `${oldRev}:webui/src/lib/preprocessor.ts`], {
    cwd: repo,
    encoding: 'utf8',
  }),
);

const oldModule = await import(pathToFileURL(oldPath).href);
const oldPreprocess = oldModule.preprocessScript as OldPreprocess;

const messages: Message[] = [];
for (const file of readdirSync(join(repo, 'dbc')).filter(f => f.endsWith('.dbc')).sort()) {
  messages.push(...parseDbc(readFileSync(join(repo, 'dbc', file), 'latin1')));
}

function firstDiff(oldCode: string, newCode: string): string {
  const oldLines = oldCode.split('\n');
  const newLines = newCode.split('\n');
  for (let i = 0; i < Math.max(oldLines.length, newLines.length); i++) {
    if (oldLines[i] !== newLines[i]) {
      return `line ${i + 1}\n    old: ${JSON.stringify(oldLines[i])}\n    new: ${JSON.stringify(newLines[i])}`;
    }
  }
  return 'no line difference';
}

function compare(label: string, source: string, scoped: Message[]): boolean {
  const oldResult = oldPreprocess(source, scoped);
  const newResult = newPreprocess(source, scoped);
  const codeSame = oldResult.code === newResult.code;
  const errorSame = oldResult.errors.length === newResult.errors.length;
  const warningSame = oldResult.warnings.length === newResult.warnings.length;
  if (codeSame && errorSame && warningSame) return true;

  console.log(`DIFF ${label}`);
  if (!codeSame) console.log(`  ${firstDiff(oldResult.code, newResult.code)}`);
  if (!errorSame) {
    console.log(`  old errors (${oldResult.errors.length}): ${oldResult.errors.join(' | ')}`);
    console.log(`  new errors (${newResult.errors.length}): ${newResult.errors.map(e => e.message).join(' | ')}`);
  }
  if (!warningSame) {
    console.log(`  old warnings (${oldResult.warnings.length}): ${oldResult.warnings.join(' | ')}`);
    console.log(`  new warnings (${newResult.warnings.length}): ${newResult.warnings.map(e => e.message).join(' | ')}`);
  }
  return false;
}

console.log(`old=${oldRev}`);
let realDiffs = 0;
const realFiles: string[] = [];
for (const dir of ['firmware/test_scripts', 'scripts/tesla']) {
  for (const file of readdirSync(join(repo, dir)).filter(f => f.endsWith('.be')).sort()) {
    realFiles.push(join(repo, dir, file));
  }
}
for (const file of realFiles) {
  const label = file.slice(repo.length + 1).replace(/\\/g, '/');
  if (!compare(label, readFileSync(file, 'utf8'), messages)) realDiffs++;
}
console.log(`real: ${realFiles.length} cases, ${realDiffs} diffs`);

let dbcDiffs = 0;
let dbcSignals = 0;
for (const message of messages) {
  const lines = [
    `var d = can_msg_get(0, ${JSON.stringify(message.name)})`,
    `var m = can_msg_new(${JSON.stringify(message.name)})`,
  ];
  for (const signal of message.signals) {
    lines.push(`msg_sig_get(d, ${JSON.stringify(signal.name)})`);
    lines.push(`msg_sig_set(d, ${JSON.stringify(signal.name)}, 1)`);
    dbcSignals++;
  }
  if (!compare(`dbc:${message.name}`, `${lines.join('\n')}\n`, [message])) dbcDiffs++;
}
console.log(`dbc: ${messages.length} messages, ${dbcSignals} signals, ${dbcDiffs} diffs`);

rmSync(tempDir, { recursive: true, force: true });
if (realDiffs > 0 || dbcDiffs > 0) process.exitCode = 1;
