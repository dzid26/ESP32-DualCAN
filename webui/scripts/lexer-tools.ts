import { createHash } from 'node:crypto';
import {
  existsSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  readdirSync,
  rmSync,
  statSync,
  writeFileSync,
} from 'node:fs';
import { spawnSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { REPO } from '../tests/helpers/corpus.ts';

export const FIRMWARE = join(REPO, 'firmware');
export const BERRY = join(FIRMWARE, 'components/berry');
export const PARSE_CACHE = join(FIRMWARE, '.cache/parse_check');
export const PARSE_BIN = join(PARSE_CACHE, process.platform === 'win32' ? 'parse_check.exe' : 'parse_check');
export const LEX_CACHE = join(FIRMWARE, '.cache/lexer-conformance');
export const LEX_BIN = join(LEX_CACHE, process.platform === 'win32' ? 'lex_dump.exe' : 'lex_dump');
export const ORACLE_CACHE = join(FIRMWARE, '.cache/preprocessor-oracle');
export const SOURCE_LOCK = join(REPO, 'webui/src/lib/berry/lexer-source.lock');

const SOURCE_MANIFEST = join(FIRMWARE, 'tools/parse_check_sources.txt');

export function berrySources(): string[] {
  const manifest = readFileSync(SOURCE_MANIFEST, 'utf8')
    .split(/\r?\n/)
    .map(line => line.trim())
    .filter(Boolean);

  const sources: string[] = [];
  for (const line of manifest) {
    if (line.includes('*')) {
      const dir = dirname(join(FIRMWARE, line));
      for (const file of readdirSync(dir).filter(f => f.endsWith('.c')).sort()) {
        sources.push(join(dir, file));
      }
    } else {
      sources.push(join(FIRMWARE, line));
    }
  }
  return sources;
}

export function parseCheckSources(): string[] {
  return [...berrySources(), join(FIRMWARE, 'tools/parse_check.c')];
}

export function lexDumpSources(): string[] {
  return [...berrySources(), join(FIRMWARE, 'tools/lex_dump.c')];
}

export function gccAvailable(): boolean {
  const result = spawnSync('gcc', ['--version'], { encoding: 'utf8' });
  return result.status === 0;
}

function buildTool(bin: string, cache: string, sources: string[]): void {
  const needsBuild = !existsSync(bin)
    || statSync(SOURCE_MANIFEST).mtimeMs > statSync(bin).mtimeMs
    || sources.some(source => statSync(source).mtimeMs > statSync(bin).mtimeMs);

  if (!needsBuild) return;

  mkdirSync(cache, { recursive: true });
  const result = spawnSync('gcc', [
    '-std=c99', '-O1', '-w',
    '-I', join(BERRY, 'src'),
    '-I', join(BERRY, 'port'),
    ...sources,
    '-o', bin,
    '-lm',
  ], { encoding: 'utf8' });

  if (result.status !== 0) {
    throw new Error(`Berry tool build failed (${bin}):\n${result.stdout ?? ''}\n${result.stderr ?? ''}`);
  }
}

export function buildParseCheck(): void {
  buildTool(PARSE_BIN, PARSE_CACHE, parseCheckSources());
}

export function buildLexDump(): void {
  buildTool(LEX_BIN, LEX_CACHE, lexDumpSources());
}

export interface LexDumpResult {
  status: number | null;
  stdout: string;
  stderr: string;
}

export function runLexDump(args: string[]): LexDumpResult {
  const result = spawnSync(LEX_BIN, args, { encoding: 'utf8' });
  return {
    status: result.status,
    stdout: result.stdout ?? '',
    stderr: result.stderr ?? '',
  };
}

export function lexDumpTokens(file: string): string {
  const result = runLexDump(['--tokens', file]);
  if (result.status !== 0) {
    throw new Error(`lex_dump failed for ${file}:\n${result.stdout}\n${result.stderr}`);
  }
  return result.stdout;
}

/**
 * Tokenize an inline source through a temp file. Some shells drop a bare `?`
 * or `#` argument, so probes never depend on argv.
 */
export function lexDumpSource(source: string): LexDumpResult {
  const dir = mkdtempSync(join(tmpdir(), 'berry-lex-'));
  const file = join(dir, 'probe.be');
  try {
    writeFileSync(file, source);
    return runLexDump(['--tokens', file]);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

export interface LexerSourceLock {
  files: Record<string, string>;
}

export function hashBerryLexer(): LexerSourceLock {
  const files: Record<string, string> = {};
  for (const file of ['src/be_lexer.c', 'src/be_lexer.h', 'src/be_parser.c']) {
    files[file] = `sha256:${createHash('sha256').update(readFileSync(join(BERRY, file))).digest('hex')}`;
  }
  return { files };
}
