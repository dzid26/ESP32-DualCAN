import test from 'node:test';
import assert from 'node:assert/strict';
import { tokenize } from '../src/lib/berry/lexer.ts';
import type { Token } from '../src/lib/berry/lexer.ts';

function texts(source: string): string[] {
  return tokenize(source).tokens
    .filter((t): t is Exclude<Token, { kind: 'eof' }> => t.kind !== 'eof')
    .map(t => t.text);
}

function kinds(source: string): string[] {
  return tokenize(source).tokens
    .filter(t => t.kind !== 'eof')
    .map(t => t.kind);
}

function firstString(source: string) {
  const token = tokenize(source).tokens.find(t => t.kind === 'string');
  assert.ok(token && token.kind === 'string', `no string token in ${JSON.stringify(source)}`);
  return token;
}

function firstParen(source: string) {
  const token = tokenize(source).tokens.find(t => t.kind === 'punct' && t.text === '(');
  assert.ok(token && token.kind === 'punct', `no '(' token in ${JSON.stringify(source)}`);
  return token;
}

test('classifies identifiers, keywords, numbers, and punctuation', () => {
  assert.deepEqual(kinds('def can_msg_get(0x3e9, ..)'), [
    'keyword', 'id', 'punct', 'number', 'punct', 'punct', 'punct',
  ]);
  assert.deepEqual(texts('<<= := == = .. . :'), ['<<=', ':=', '==', '=', '..', '.', ':']);
});

test('marks a call parenthesis only when nothing precedes it', () => {
  assert.equal(firstParen('foo(').callable, true);
  assert.equal(firstParen('foo (').callable, false);
  assert.equal(firstParen('foo\t(').callable, false);
  assert.equal(firstParen('foo\n(').callable, false);
  assert.equal(firstParen('foo\r\n(').callable, false);
  assert.equal(firstParen('foo# note\n(').callable, false);
  assert.equal(firstParen('foo#- note -#(').callable, false);
  assert.equal(firstParen('#- note -#foo(').callable, true);
  assert.equal(firstParen('(').callable, true, 'source start has no preceding whitespace');
  assert.equal(firstParen('foo((').callable, true);
});

test('skips line and block comments and reports the block start', () => {
  const line = tokenize('# note ) "\nfoo');
  assert.deepEqual(line.errors, []);
  assert.deepEqual(texts('# note ) "\nfoo'), ['foo']);

  const block = tokenize('#- note ) "\nmore -#foo');
  assert.deepEqual(block.errors, []);
  assert.deepEqual(texts('#- note ) "\nmore -#foo'), ['foo']);
  assert.equal(block.tokens[0].span.line, 2);
  assert.equal(block.tokens[0].span.column, 8);

  const open = tokenize('#- open\nstill open');
  assert.equal(open.errors.length, 1);
  assert.equal(open.errors[0].code, 'lex/unterminated-comment');
  assert.equal(open.errors[0].span.line, 1);
  assert.equal(open.errors[0].span.column, 1);
  assert.equal(open.tokens.at(-1)?.kind, 'eof');
});

test('does not nest block comments', () => {
  const r = tokenize('#- #- inner -#foo');
  assert.deepEqual(r.errors, []);
  assert.deepEqual(texts('#- #- inner -#foo'), ['foo']);
});

test('decodes simple, hex, unicode, and octal escapes', () => {
  assert.equal(firstString(String.raw`"a\nb"`).value, 'a\nb');
  assert.equal(firstString(String.raw`"a\\b"`).value, 'a\\b');
  assert.equal(firstString(String.raw`"a\"b"`).value, 'a"b');
  assert.equal(firstString(String.raw`'a\'b'`).value, "a'b");
  assert.equal(firstString(String.raw`"a\x41b"`).value, 'aAb');
  assert.equal(firstString(String.raw`"a\u0041b"`).value, 'aAb');
  assert.equal(firstString(String.raw`"a\123b"`).value, 'aSb');
  assert.equal(firstString(String.raw`"a\000b"`).value, 'a\0b');
  assert.equal(firstString(String.raw`"a\?b"`).value, 'a?b');
});

test('reports invalid escapes and keeps scanning', () => {
  const octal = tokenize(String.raw`"a\0b"`);
  assert.equal(octal.errors[0].code, 'lex/invalid-octal');
  assert.equal(firstString(String.raw`"a\0b"`).value, 'ab');

  const unknown = tokenize(String.raw`"a\qb"`);
  assert.equal(unknown.errors[0].code, 'lex/unknown-escape');

  const hex = tokenize(String.raw`"a\x4"`);
  assert.equal(hex.errors[0].code, 'lex/invalid-hex');

  const unicode = tokenize(String.raw`"a\u004"`);
  assert.equal(unicode.errors[0].code, 'lex/invalid-unicode');
});

test('rejects raw newlines and resumes after them', () => {
  const r = tokenize('"a\nb"');
  assert.equal(r.errors[0].code, 'lex/unfinished-string');
  assert.equal(r.errors[0].span.line, 1);
  assert.equal(r.errors[0].span.column, 1);
  assert.equal(r.tokens[0].kind, 'string');
  assert(r.tokens.some(t => t.kind === 'id' && t.text === 'b'), 'tokenizer should resume');
});

test('concatenates adjacent literals across delimiters only', () => {
  const adjacent = firstString('"a" "b"');
  assert.equal(adjacent.value, 'ab');
  assert.equal(adjacent.span.start, 0);
  assert.equal(adjacent.span.end, '"a" "b"'.length);

  const commented = firstString('"a" # note\n "b"');
  assert.equal(commented.value, 'ab');

  const singleQuoted = firstString(`"a" 'b'`);
  assert.equal(singleQuoted.value, 'ab');

  const concatenated = tokenize('"a" .. "b"');
  assert.equal(concatenated.tokens.filter(t => t.kind === 'string').length, 2);
  assert.equal(concatenated.tokens.some(t => t.kind === 'punct' && t.text === '..'), true);
});

test('counts every line ending form once', () => {
  for (const source of ['a\nb', 'a\rb', 'a\r\nb', 'a\n\rb']) {
    const b = tokenize(source).tokens.find(t => t.kind === 'id' && t.text === 'b');
    assert.ok(b);
    assert.equal(b.span.line, 2, JSON.stringify(source));
    assert.equal(b.span.column, 1, JSON.stringify(source));
  }

  const block = tokenize('#- one\ntwo\r\nthree -#x');
  const x = block.tokens.find(t => t.kind === 'id' && t.text === 'x');
  assert.ok(x);
  assert.equal(x.span.line, 3);
  assert.equal(x.span.column, 9);
});

test('tracks columns after indentation and tabs', () => {
  const r = tokenize('a\n  b\n\tc');
  const b = r.tokens.find(t => t.kind === 'id' && t.text === 'b');
  const c = r.tokens.find(t => t.kind === 'id' && t.text === 'c');
  assert.ok(b && c);
  assert.equal(b.span.column, 3);
  assert.equal(c.span.column, 2);
});

test('token spans are ordered, in bounds, and gap-concat the source', () => {
  const source = [
    '# line comment',
    '#- block',
    'comment -#',
    'var d = can_msg_get(0, "DAS" " _bodyControls")',
    "var s = 'a\\'b' + \"c\\x41\"",
    'msg_sig_set(d, "Sig", (1 + 2))',
  ].join('\n');
  const r = tokenize(source);
  assert.deepEqual(r.errors, []);

  let cursor = 0;
  let rebuilt = '';
  for (const token of r.tokens) {
    if (token.kind === 'eof') break;
    assert.ok(token.span.start >= cursor);
    assert.ok(token.span.end >= token.span.start);
    assert.ok(token.span.end <= source.length);
    rebuilt += source.slice(cursor, token.span.start);
    rebuilt += source.slice(token.span.start, token.span.end);
    cursor = token.span.end;
  }
  rebuilt += source.slice(cursor);
  assert.equal(rebuilt, source);
});

test('diagnostics are in bounds and carry a positive location', () => {
  const r = tokenize('#- open\n"a\\q"\n@');
  assert(r.errors.length > 0);
  for (const error of r.errors) {
    assert.ok(error.span.start >= 0 && error.span.end <= '#- open\n"a\\q"\n@'.length);
    assert.ok(error.span.line >= 1);
    assert.ok(error.span.column >= 1);
  }
});

test('classifies malformed numbers without throwing', () => {
  assert.equal(tokenize('0x').errors[0].code, 'lex/malformed-number');
  assert.equal(tokenize('123abc').errors[0].code, 'lex/malformed-number');
  assert.equal(tokenize('1e').errors[0].code, 'lex/malformed-number');
  assert.deepEqual(tokenize('0x3e9 1.5 .5 1e-3').errors, []);
});

test('reads Berry real forms and recovers after a malformed suffix', () => {
  assert.deepEqual(tokenize('1. .5 1e3 1..2').errors, []);
  assert.deepEqual(texts('1. .5 1e3 1..2'), ['1.', '.5', '1e3', '1', '..', '2']);

  const malformed = tokenize('1.foo');
  assert.equal(malformed.errors[0].code, 'lex/malformed-number');
  assert.equal(malformed.tokens[0].text, '1.');
  assert(malformed.tokens.some(t => t.kind === 'id' && t.text === 'foo'));
});

test('reports a stray character and continues', () => {
  const r = tokenize('a @ b');
  assert.equal(r.errors[0].code, 'lex/stray-character');
  assert.deepEqual(texts('a @ b'), ['a', 'b']);
});

test('emits EOF for empty input', () => {
  const r = tokenize('');
  assert.equal(r.tokens.length, 1);
  assert.equal(r.tokens[0].kind, 'eof');
  assert.equal(r.tokens[0].span.line, 1);
  assert.equal(r.tokens[0].span.column, 1);
});

test('never throws on malformed input', () => {
  const samples = [
    '', '#', '#-', '#- -#', '"', "'", '\\', '"\\', '0x', '1e', '@',
    'foo(', 'foo (', 'foo#\n(', '#- x\ny', 'a\r\nb', 'a\n\rb',
    '"a\\x"', '"a\\u"', '"a\\0"', '"a\\777"',
  ];
  for (const sample of samples) {
    assert.doesNotThrow(() => tokenize(sample), JSON.stringify(sample));
  }
});
