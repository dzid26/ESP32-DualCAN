import test from 'node:test';
import assert from 'node:assert/strict';
import { tokenize } from '../src/lib/berry/lexer.ts';
import { buildCalls, render } from '../src/lib/berry/calls.ts';
import type { CallNode, Rewriter } from '../src/lib/berry/calls.ts';

function build(source: string) {
  const lexed = tokenize(source);
  const parsed = buildCalls(lexed.tokens);
  const callAt = new Map<number, CallNode>();
  for (const call of parsed.calls) callAt.set(call.nameTokenIndex, call);
  return { source, tokens: lexed.tokens, parsed, callAt };
}

function onlyCall(source: string): CallNode {
  const { parsed } = build(source);
  assert.equal(parsed.calls.length, 1, `expected one call in ${JSON.stringify(source)}`);
  return parsed.calls[0];
}

test('finds a plain call and its argument ranges', () => {
  const call = onlyCall('can_msg_get(0, "DAS_bodyControls")');
  assert.equal(call.calleeName, 'can_msg_get');
  assert.equal(call.args.length, 2);
  assert.equal(call.balanced, true);
  assert.equal(call.isMember, false);
  assert.equal(call.isDef, false);
  assert.equal(call.args[0].span.start, 'can_msg_get('.length);
  assert.equal(call.args[0].span.end, 'can_msg_get(0'.length);
  assert.equal(call.args[1].span.start, 'can_msg_get(0,'.length);
  assert.equal(call.args[1].span.end, 'can_msg_get(0, "DAS_bodyControls"'.length);
});

test('keeps surrounding trivia in argument spans', () => {
  const { source } = build('foo( a , b )');
  const call = onlyCall(source);
  assert.equal(source.slice(call.args[0].span.start, call.args[0].span.end), ' a ');
  assert.equal(source.slice(call.args[1].span.start, call.args[1].span.end), ' b ');
});

test('distinguishes zero arguments from empty ones', () => {
  assert.equal(onlyCall('foo()').args.length, 0);
  assert.equal(onlyCall('foo( )').args.length, 0);
  assert.equal(onlyCall('foo(,)').args.length, 2);
  assert.equal(onlyCall('foo(a,)').args.length, 2);
  assert.equal(onlyCall('foo(,a)').args.length, 2);
  assert.equal(onlyCall('foo(,)').args[0].first, -1);
  assert.equal(onlyCall('foo(,)').args[1].first, -1);
});

test('splits only on top-level commas', () => {
  const source = 'foo([1, 2], {a: 1}, bar(3, 4))';
  const foo = build(source).parsed.calls.find(c => c.calleeName === 'foo');
  assert.ok(foo);
  assert.equal(foo.args.length, 3);
  assert.equal(onlyCall('foo("a,b", \'c)d\')').args.length, 2);
  assert.equal(onlyCall('foo(1 #- , ) -# , 2)').args.length, 2);
});

test('finds nested calls inside an outer argument', () => {
  const source = 'msg_sig_set(msg, "X", (int(msg_sig_get(msg, "Y")) + 1) % 16)';
  const { parsed, callAt } = build(source);
  const outer = parsed.calls.find(c => c.calleeName === 'msg_sig_set');
  const inner = parsed.calls.find(c => c.calleeName === 'msg_sig_get');
  assert.ok(outer && inner);
  assert.equal(outer.args.length, 3);
  assert(inner.nameTokenIndex > outer.args[2].first);
  assert(inner.nameTokenIndex <= outer.args[2].last);
  assert.equal(callAt.get(inner.nameTokenIndex), inner);
  assert.equal(callAt.get(parsed.calls.find(c => c.calleeName === 'int')!.nameTokenIndex)?.calleeName, 'int');
});

test('marks member calls and definitions without treating them as calls to rewrite', () => {
  const member = onlyCall('foo.bar(1)');
  assert.equal(member.calleeName, 'bar');
  assert.equal(member.isMember, true);

  const spacedMember = onlyCall('foo . bar(1)');
  assert.equal(spacedMember.isMember, true);

  const definition = build('def can_msg_get(x)\n  return x\nend');
  assert.equal(definition.parsed.calls.length, 1);
  assert.equal(definition.parsed.calls[0].isDef, true);

  const spacedDefinition = build('def f (x)\n  return x\nend');
  assert.equal(spacedDefinition.parsed.calls.length, 1);
  assert.equal(spacedDefinition.parsed.calls[0].spaced, true);
  assert.equal(spacedDefinition.parsed.calls[0].isDef, true);
});

test('records a spaced parenthesis as grouping', () => {
  const spaced = build('can_msg_get (0, "DAS_bodyControls")');
  assert.equal(spaced.parsed.calls.length, 1);
  assert.equal(spaced.parsed.calls[0].calleeName, 'can_msg_get');
  assert.equal(spaced.parsed.calls[0].spaced, true);
  assert.equal(spaced.parsed.calls[0].balanced, true);

  const commented = build('can_msg_get# note\n(0, "DAS_bodyControls")');
  assert.equal(commented.parsed.calls[0].spaced, true);

  const member = build('foo. can_msg_get (0, "DAS_bodyControls")');
  assert.equal(member.parsed.calls[0].spaced, true);
  assert.equal(member.parsed.calls[0].isMember, true);
});

test('strips balanced outer grouping parentheses from an argument', () => {
  const plain = onlyCall('foo(bar)');
  assert.equal(plain.args[0].first, plain.args[0].last);

  const grouped = onlyCall('foo(((bar)))');
  assert.equal(grouped.args[0].first, grouped.args[0].last);

  const expression = onlyCall('foo((a) + (b))');
  assert.notEqual(expression.args[0].first, expression.args[0].last);
});

test('reports unbalanced and mismatched brackets without losing the file', () => {
  const open = build('foo(1, 2');
  assert.equal(open.parsed.calls[0].balanced, false);
  assert.equal(open.parsed.calls[0].rparenIndex, -1);
  assert.equal(open.parsed.calls[0].args.length, 0);
  assert(open.parsed.errors.some(e => e.code === 'parse/unclosed'));

  const stray = build('foo(1, 2))');
  assert.equal(stray.parsed.calls[0].balanced, true);
  assert(stray.parsed.errors.some(e => e.code === 'parse/unmatched-close'));

  const mismatched = build('foo(1, [2, 3)');
  assert(mismatched.parsed.errors.some(e => e.code === 'parse/unmatched-close'));
  assert(mismatched.parsed.errors.some(e => e.code === 'parse/unclosed'));
});

test('a no-op rewriter reproduces the source exactly', () => {
  const source = [
    '# line comment',
    '#- block',
    'comment -#',
    'var d = can_msg_get(0, "DAS" " _bodyControls")',
    "var s = 'a\\'b' + \"c\\x41\"",
    'msg_sig_set(d, "Sig", (1 + 2))',
    'foo(1, [2, 3), bar("x,y")',
  ].join('\n');
  const { tokens, callAt } = build(source);
  assert.equal(render(source, tokens, callAt, () => null), source);
});

test('rewrites a nested call inside a call that passes', () => {
  const source = 'print(can_msg_get(0, "DAS_bodyControls"))';
  const { tokens, callAt } = build(source);
  const rewrite: Rewriter = (call, renderArg) => {
    if (call.calleeName !== 'can_msg_get') return null;
    return `GET(${renderArg(call.args[0])})`;
  };
  assert.equal(render(source, tokens, callAt, rewrite), 'print(GET(0))');
});

test('rewrites nested calls inside a replacement argument', () => {
  const source = 'msg_sig_set(msg, "S", msg_sig_get(msg, "T"))';
  const { tokens, callAt } = build(source);
  const rewrite: Rewriter = (call, renderArg) => {
    if (call.calleeName === 'msg_sig_get') return `GET(${renderArg(call.args[0])}, ${renderArg(call.args[1])})`;
    if (call.calleeName === 'msg_sig_set') {
      return `SET(${renderArg(call.args[0])}, ${renderArg(call.args[1])}, ${renderArg(call.args[2])})`;
    }
    return null;
  };
  assert.equal(render(source, tokens, callAt, rewrite), 'SET(msg, "S", GET(msg, "T"))');
});

test('leaves member calls but still rewrites their arguments', () => {
  const source = 'foo.bar(0, can_msg_get(0, "DAS_bodyControls"))';
  const { tokens, callAt } = build(source);
  const rewrite: Rewriter = (call, renderArg) => {
    if (call.isMember) return null;
    if (call.calleeName === 'can_msg_get') return `GET(${renderArg(call.args[0])})`;
    return null;
  };
  assert.equal(render(source, tokens, callAt, rewrite), 'foo.bar(0, GET(0))');
});

test('leaves a definition but rewrites calls in its body', () => {
  const source = 'def can_msg_get(x)\n  return can_msg_get(0, "DAS_bodyControls")\nend';
  const { tokens, callAt } = build(source);
  const rewrite: Rewriter = (call, renderArg) => {
    if (call.isDef) return null;
    if (call.calleeName === 'can_msg_get') return `GET(${renderArg(call.args[0])})`;
    return null;
  };
  assert.equal(
    render(source, tokens, callAt, rewrite),
    'def can_msg_get(x)\n  return GET(0)\nend',
  );
});

test('does not rewrite an unbalanced call', () => {
  const source = 'msg_sig_set(msg, "S", (1 + 2';
  const { tokens, callAt } = build(source);
  const rewrite: Rewriter = () => 'BROKEN';
  assert.equal(render(source, tokens, callAt, rewrite), source);
});

test('preserves a line comment in a rewritten value argument', () => {
  const source = [
    'msg_sig_set(msg, "UI_powertrainControlCounter",',
    '  1  # note',
    '  )',
  ].join('\n');
  const { tokens, callAt } = build(source);
  const rewrite: Rewriter = (call, renderArg) => {
    if (call.calleeName !== 'msg_sig_set') return null;
    return `SET(${renderArg(call.args[2])})`;
  };
  assert.equal(render(source, tokens, callAt, rewrite), [
    'SET(1  # note',
    '  )',
  ].join('\n'));
});
