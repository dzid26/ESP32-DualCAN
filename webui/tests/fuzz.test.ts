import test from 'node:test';
import assert from 'node:assert/strict';
import { preprocessScript, formatDiagnostic } from '../src/lib/preprocessor.ts';
import { tokenize } from '../src/lib/berry/lexer.ts';
import { buildCalls } from '../src/lib/berry/calls.ts';
import type { ArgRange } from '../src/lib/berry/calls.ts';
import type { Token } from '../src/lib/berry/lexer.ts';
import { loadDbcMessages } from './helpers/corpus.ts';

const ITERATIONS = Number(process.env.FUZZ_ITERATIONS ?? 200);
const messages = loadDbcMessages();

function mulberry32(seed: number): () => number {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const FRAGMENTS = [
  '# line comment ) " can_msg_get(0, "x")\n',
  '#- block ) " -#',
  '"string ) # can_msg_get(0, \\"x\\")"',
  "'single \\' quote'",
  'can_msg_get(0, "DAS_bodyControls")',
  'can_msg_new("DAS_bodyControls")',
  'can_msg_new(0x3e9, 8)',
  'var d = can_msg_get(0, "DAS_bodyControls")\n',
  'msg_sig_get(d, "DAS_hazardLightRequest")',
  'msg_sig_set(d, "DAS_hazardLightRequest", 1)',
  'msg_sig_set(msg, "UI_powertrainControlCounter", (int(msg_sig_get(msg, "UI_powertrainControlCounter")) + 1) % 16)',
  'msg_sig_set(msg, "X", (1 + 2',
  'foo (1)',
  'def can_msg_get(x)\n  return x\nend\n',
  'foo.can_msg_get(0, "DAS_bodyControls")',
  'can_msg_new(0x294)',
  'can_msg_get(0, prefix .. "DAS_bodyControls")',
  'random text @',
  'var x = 1\n',
  'print("ok")\n',
  '#- open\n',
  '"unfinished\n',
];

function generate(rng: () => number): string {
  const count = 1 + Math.floor(rng() * 12);
  let source = '';
  for (let i = 0; i < count; i++) {
    source += FRAGMENTS[Math.floor(rng() * FRAGMENTS.length)];
  }
  return source;
}

function quotedArg(arg: ArgRange | undefined, tokens: readonly Token[]): boolean {
  if (arg === undefined || arg.first < 0 || arg.first !== arg.last) return false;
  return tokens[arg.first].kind === 'string';
}

function assertNoQuotedDbcNames(code: string): void {
  const lexed = tokenize(code);
  const parsed = buildCalls(lexed.tokens);
  for (const call of parsed.calls) {
    if (call.isMember || call.isDef || call.spaced || !call.balanced) continue;
    if (call.calleeName === 'can_msg_get') {
      assert(!quotedArg(call.args[1], lexed.tokens), `quoted message left:\n${code}`);
    } else if (call.calleeName === 'can_msg_new') {
      assert(
        !call.args.some(arg => quotedArg(arg, lexed.tokens)),
        `quoted message left:\n${code}`,
      );
    } else if (call.calleeName === 'msg_sig_get' || call.calleeName === 'msg_sig_set') {
      assert(!quotedArg(call.args[1], lexed.tokens), `quoted signal left:\n${code}`);
    }
  }
}

function assertSpans(source: string, result: ReturnType<typeof preprocessScript>): void {
  for (const diagnostic of [...result.errors, ...result.warnings]) {
    assert(
      diagnostic.span.start >= 0 && diagnostic.span.end <= source.length,
      `span out of bounds: ${diagnostic.code}`,
    );
    assert(diagnostic.span.line >= 1 && diagnostic.span.column >= 1);
  }
}

test('fuzz invariants', () => {
  const rng = mulberry32(0xc0ffee);
  for (let iteration = 0; iteration < ITERATIONS; iteration++) {
    const source = generate(rng);
    const first = preprocessScript(source, messages);
    const second = preprocessScript(source, messages);

    assert.equal(first.code, second.code, `nondeterministic output:\n${source}`);
    assert.deepEqual(
      first.errors.map(formatDiagnostic),
      second.errors.map(formatDiagnostic),
      `nondeterministic errors:\n${source}`,
    );
    assert.deepEqual(
      first.warnings.map(formatDiagnostic),
      second.warnings.map(formatDiagnostic),
      `nondeterministic warnings:\n${source}`,
    );
    assertSpans(source, first);

    if (first.errors.length === 0) {
      assertNoQuotedDbcNames(first.code);
      const again = preprocessScript(first.code, messages);
      assert.equal(
        again.errors.length,
        0,
        `reprocessed output gained errors:\n${source}\n---\n${first.code}\n---\n${again.errors.map(formatDiagnostic).join('; ')}`,
      );
      assert.equal(
        again.code,
        first.code,
        `not idempotent:\n${source}\n---\n${first.code}\n---\n${again.code}`,
      );
    }
  }
});
