import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { tokenize, type Token } from '../src/lib/berry/lexer.ts';
import { KEYWORD_TYPES, PUNCTUATION_TYPES } from '../src/lib/berry/lexer-tables.ts';
import { realScriptFiles } from './helpers/corpus.ts';
import {
  SOURCE_LOCK,
  buildLexDump,
  gccAvailable,
  hashBerryLexer,
  lexDumpSource,
  lexDumpTokens,
} from '../scripts/lexer-tools.ts';

interface TokenRow {
  type: number;
  line: number;
  /** Hex-encoded UTF-8 text for identifiers and strings. */
  text?: string;
  /** Parsed numeric value for integers and reals. */
  number?: bigint | number;
}

const TOKEN_ID = 2;
const TOKEN_INTEGER = 3;
const TOKEN_REAL = 4;
const TOKEN_STRING = 5;
const OPT_SPACE_LBK = 38;
const OPT_CALL_LBK = 39;

const hasGcc = gccAvailable();
if (hasGcc) buildLexDump();

function hex(text: string): string {
  return Buffer.from(text, 'utf8').toString('hex');
}

function decodeHex(text: string): string {
  return Buffer.from(text, 'hex').toString('utf8');
}

function tsType(token: Exclude<Token, { kind: 'eof' }>): number {
  switch (token.kind) {
    case 'id':
      return TOKEN_ID;
    case 'number':
      return /^0[xX]/.test(token.text) || !/[.eE]/.test(token.text)
        ? TOKEN_INTEGER
        : TOKEN_REAL;
    case 'string':
      return TOKEN_STRING;
    case 'keyword':
      return KEYWORD_TYPES[token.text];
    case 'punct':
      if (token.text === '(') return token.callable ? OPT_CALL_LBK : OPT_SPACE_LBK;
      return PUNCTUATION_TYPES[token.text];
  }
}

function tsRows(source: string): TokenRow[] {
  return tokenize(source).tokens
    .filter((token): token is Exclude<Token, { kind: 'eof' }> => token.kind !== 'eof')
    .map(token => {
      const type = tsType(token);
      if (token.kind === 'number') {
        return {
          type,
          line: token.span.line,
          number: type === TOKEN_INTEGER ? BigInt(token.text) : Math.fround(Number(token.text)),
        };
      }
      return {
        type,
        line: token.span.line,
        text: token.kind === 'string' ? hex(token.value) : hex(token.text),
      };
    });
}

function cRows(output: string): TokenRow[] {
  return output.split(/\r?\n/).filter(Boolean).map(line => {
    const [typeText, value, lineText] = line.split('\t');
    const type = Number(typeText);
    if (type === TOKEN_INTEGER) {
      return { type, line: Number(lineText), number: BigInt(decodeHex(value)) };
    }
    if (type === TOKEN_REAL) {
      return { type, line: Number(lineText), number: Number(decodeHex(value)) };
    }
    return { type, line: Number(lineText), text: value };
  });
}

function realClose(actual: number, expected: number): boolean {
  if (actual === expected) return true;
  return Math.abs(actual - expected) <= Math.max(1e-9, Math.abs(expected) * 1e-6);
}

function describe(source: string, index: number): string {
  const text = source.length > 60 ? `${source.slice(0, 60)}...` : source;
  return `token ${index} of ${JSON.stringify(text)}`;
}

function assertMatchesC(source: string, output: string): void {
  const lexed = tokenize(source);
  assert.deepEqual(
    lexed.errors.map(error => `${error.code} at ${error.span.line}:${error.span.column}`),
    [],
    `TypeScript lexer errors for ${JSON.stringify(source)}`,
  );
  const ts = tsRows(source);
  const c = cRows(output);
  assert.equal(ts.length, c.length, `token count for ${JSON.stringify(source)}`);

  for (let i = 0; i < c.length; i++) {
    const actual = ts[i];
    const expected = c[i];
    const where = describe(source, i);
    assert.equal(actual.type, expected.type, `type of ${where}`);

    // Berry's scanner consumes trailing trivia after a string, so its current
    // line is the line after the literal, not the line where it started.
    if (expected.type !== TOKEN_STRING) {
      assert.equal(actual.line, expected.line, `line of ${where}`);
    }

    if (expected.type === TOKEN_INTEGER) {
      assert.equal(actual.number, expected.number, `value of ${where}`);
    } else if (expected.type === TOKEN_REAL) {
      assert.ok(
        realClose(Number(actual.number), Number(expected.number)),
        `value of ${where}: ${String(actual.number)} != ${String(expected.number)}`,
      );
    } else if (expected.type === TOKEN_ID || expected.type === TOKEN_STRING) {
      assert.equal(actual.text, expected.text, `value of ${where}`);
    }
  }
}

function hasFString(tokens: readonly Token[]): boolean {
  return tokens.some((token, index) => {
    const next = tokens[index + 1];
    return token.kind === 'id'
      && token.text === 'f'
      && next?.kind === 'string'
      && next.span.start === token.span.end;
  });
}

const edgeCases = [
  '# line comment\nvar a = 1',
  '#- block\ncomment -#\nvar a = 1',
  '#- quote " and \' and # and - # -#\nvar a = 1',
  '# comment with ) " \'\nvar a = 1',
  'var a = "text # not comment"',
  'var a = "text #- not block -#"',
  'var a = \'single " double\'',
  'var a = "a\\nb\\tc\\\\d\\"e\\\'f\\?g"',
  'var a = "a\\ab\\bc\\fd\\re\\vf"',
  'var a = "a\\x41b\\u0042c\\101d"',
  'var a = "a" "b" \'c\'',
  'var a = "a" # note\n "b"',
  'var a = 0x3E9 + 0Xff',
  'var a = 1. + .5 + 1.5 + 1e3 + 1E-3',
  'var a = 1..2',
  'var a = 1.2..3',
  'var a = (1)',
  'foo(1, bar(2, 3), [4, 5], {6: 7})',
  'foo (1)',
  'foo# note\n(1)',
  'foo#- note -#(1)',
  'if true return 1 elif false return 2 else return 3 end',
  'while true break end',
  'for x : [1] continue end',
  'def f(a, b) return a end',
  'class C end',
  'var a = nil',
  'import x as y',
  'try raise "x" except .. end',
  'static var a = 1',
  'do var a = 1 end',
  'var a = 1 ? 2 : 3',
  'var a = 1 << 2 >> 3 & 4 | 5 ^ 6 ~ 7',
  'var a = true && false || !true',
  'a += 1 a -= 1 a *= 2 a /= 2 a %= 2 a &= 1 a |= 1 a ^= 1 a <<= 1 a >>= 1',
  'var a = b->c',
  'var a := 1',
  'a\r\nb\rc\nd',
  'a\n\rb',
  '#- one\ntwo\r\nthree -#x',
  'var s = "line\\nbreak"',
];

test('the generated tables are locked to the vendored lexer sources', () => {
  const lock = JSON.parse(readFileSync(SOURCE_LOCK, 'utf8')) as unknown;
  assert.deepEqual(
    hashBerryLexer(),
    lock,
    'Berry lexer sources changed. Run: node webui/scripts/gen-lexer-tables.ts',
  );
});

test('matches the C lexer on the real script corpus', {
  skip: hasGcc ? false : process.env.CI ? false : 'gcc is not available',
}, t => {
  for (const file of realScriptFiles()) {
    const source = readFileSync(file, 'utf8');
    if (hasFString(tokenize(source).tokens)) {
      t.diagnostic(`skipping f-string file (opaque in the TypeScript lexer): ${file}`);
      continue;
    }
    assertMatchesC(source, lexDumpTokens(file));
  }
});

test('matches the C lexer on valid edge cases', {
  skip: hasGcc ? false : process.env.CI ? false : 'gcc is not available',
}, () => {
  for (const source of edgeCases) {
    const result = lexDumpSource(source);
    assert.equal(result.status, 0, `lex_dump rejected ${JSON.stringify(source)}:\n${result.stderr}`);
    assertMatchesC(source, result.stdout);
  }
});
