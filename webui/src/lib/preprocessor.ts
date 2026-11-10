/**
 * Berry script preprocessor: converts `.be` source to `.bep` runtime format.
 *
 * Design: self-contained preprocessed scripts with no runtime DBC dependency.
 * Every signal's metadata is inlined as Berry code.
 *
 * Conversion rules:
 *   can_msg_get(bus, "msg")        -> can_msg_get(bus, 0xID)
 *   can_msg_new("msg")             -> can_msg_new(0xID, dlc)
 *   msg_sig_get(draft, "sig")      -> msg_sig_get(draft, sb, len, be, signed, scale, offset)
 *   msg_sig_set(draft, "sig", val) -> msg_sig_set(draft, sb, len, be, signed, scale, offset, val)
 *
 * One tokenizer pass mirrors firmware/components/berry/src/be_lexer.c: `#` line
 * comments, `#- ... -#` block comments, both quote styles and their escapes,
 * adjacent string literals, and the OptCallLBK / OptSpaceLBK distinction for
 * `(`. Calls are parsed into nodes with argument ranges, then rendered by
 * replacing whole source spans, so untouched code stays byte for byte.
 *
 * A call that cannot be resolved is replaced with `assert(false, "...")`, a
 * failure form that parses in Berry expression position, and reported in
 * errors[] with a line and column. Warnings report ambiguity but do not block.
 */

import type { Message, Signal } from '../dbc/parser.ts';
import type { Diagnostic, Span } from './berry/diagnostic.ts';
import { formatDiagnostic } from './berry/diagnostic.ts';
import { tokenize } from './berry/lexer.ts';
import type { Token } from './berry/lexer.ts';
import { buildCalls, render } from './berry/calls.ts';
import type { ArgRange, CallNode, ParseResult } from './berry/calls.ts';

export { formatDiagnostic } from './berry/diagnostic.ts';
export type { Diagnostic, Span } from './berry/diagnostic.ts';

/* ---- Scripting API version ---- */
// The firmware reports scripting_api_version in system.info; the web UI warns
// before uploading a script when versions don't match. Version 2: top-level
// names are private to each script (compiled as locals) instead of sharing the
// VM global table.
export const SCRIPTING_API_VERSION = 2;

/** Result of preprocessing one script. */
export interface PreprocessResult {
  code: string;
  errors: Diagnostic[];
  warnings: Diagnostic[];
}

/* Build the signal-decode/encode argument list from a Signal. */
function sigMeta(signal: Signal): string {
  return [
    signal.startBit,
    signal.bitLength,
    signal.isBigEndian ? 'true' : 'false',
    signal.isSigned ? 'true' : 'false',
    signal.scale,
    signal.offset,
  ].join(', ');
}

/* ---- Message index, cached by array identity ---- */

interface MessageIndex {
  byName: Map<string, Message>;
  bySignal: Map<string, Array<{ msg: Message; sig: Signal }>>;
}

// Callers must treat the messages array as immutable; the DBC store replaces
// the array when the loaded DBC changes.
const messageIndexCache = new WeakMap<readonly Message[], MessageIndex>();

function getMessageIndex(messages: readonly Message[]): MessageIndex {
  const cached = messageIndexCache.get(messages);
  if (cached !== undefined) return cached;

  const byName = new Map<string, Message>();
  const bySignal = new Map<string, Array<{ msg: Message; sig: Signal }>>();
  for (const msg of messages) {
    if (!byName.has(msg.name)) byName.set(msg.name, msg);
    for (const sig of msg.signals) {
      const list = bySignal.get(sig.name);
      if (list !== undefined) list.push({ msg, sig });
      else bySignal.set(sig.name, [{ msg, sig }]);
    }
  }

  const index: MessageIndex = { byName, bySignal };
  messageIndexCache.set(messages, index);
  return index;
}

/* ---- Diagnostics ---- */

class DiagnosticBag {
  readonly errors: Diagnostic[] = [];
  readonly warnings: Diagnostic[] = [];
  private readonly errorKeys = new Set<string>();
  private readonly warningKeys = new Set<string>();

  error(code: string, message: string, span: Span): void {
    const key = `${code}\u0000${message}\u0000${span.start}`;
    if (this.errorKeys.has(key)) return;
    this.errorKeys.add(key);
    this.errors.push({ code, message, span });
  }

  warn(code: string, message: string, span: Span): void {
    // Warnings collapse on text so repeated ambiguity reports do not spam.
    const key = `${code}\u0000${message}`;
    if (this.warningKeys.has(key)) return;
    this.warningKeys.add(key);
    this.warnings.push({ code, message, span });
  }
}

/* Quote a string for generated Berry source. */
function berryString(value: string): string {
  return `"${value
    .replace(/\\/g, '\\\\')
    .replace(/"/g, '\\"')
    .replace(/\n/g, '\\n')
    .replace(/\r/g, '\\r')
    .replace(/\t/g, '\\t')}"`;
}

/* A failure expression that is valid wherever the original call was. */
function berryFailure(message: string): string {
  return `assert(false, ${berryString(message)})`;
}

/* ---- Argument inspection ---- */

function isEmpty(arg: ArgRange): boolean {
  return arg.first < 0 || arg.last < arg.first;
}

function singleToken(arg: ArgRange, tokens: readonly Token[]): Token | null {
  if (isEmpty(arg) || arg.first !== arg.last) return null;
  return tokens[arg.first];
}

function singleString(arg: ArgRange, tokens: readonly Token[]): (Token & { kind: 'string' }) | null {
  const token = singleToken(arg, tokens);
  return token?.kind === 'string' ? token : null;
}

function singleId(arg: ArgRange, tokens: readonly Token[]): string | null {
  const token = singleToken(arg, tokens);
  return token?.kind === 'id' ? token.text : null;
}

function hasString(arg: ArgRange, tokens: readonly Token[]): boolean {
  if (isEmpty(arg)) return false;
  for (let i = arg.first; i <= arg.last; i++) {
    if (tokens[i].kind === 'string') return true;
  }
  return false;
}

function firstStringSpan(arg: ArgRange, tokens: readonly Token[]): Span {
  if (!isEmpty(arg)) {
    for (let i = arg.first; i <= arg.last; i++) {
      const token = tokens[i];
      if (token.kind === 'string') return token.span;
    }
  }
  return arg.span;
}

/**
 * First string token at the argument's own level. Strings inside a nested call
 * belong to that call, so `can_msg_get(0, can_msg_new("Name"))` is not a
 * computed name.
 */
function topLevelStringSpan(
  arg: ArgRange,
  tokens: readonly Token[],
  callAt: ReadonlyMap<number, CallNode>,
): Span | null {
  if (isEmpty(arg)) return null;
  for (let i = arg.first; i <= arg.last; i++) {
    const call = callAt.get(i);
    if (call !== undefined && call.balanced && call.rparenIndex <= arg.last) {
      i = call.rparenIndex;
      continue;
    }
    if (tokens[i].kind === 'string') return tokens[i].span;
  }
  return null;
}

/** The call that fills the whole argument, after stripping grouping parens. */
function soleCallInArg(
  arg: ArgRange,
  tokens: readonly Token[],
  callAt: ReadonlyMap<number, CallNode>,
): CallNode | null {
  if (isEmpty(arg)) return null;
  const call = callAt.get(arg.first);
  if (call === undefined) return null;
  return call.rparenIndex === arg.last ? call : null;
}

/** Message name carried by an inline can_msg_get / can_msg_new call. */
function messageNameFromCall(call: CallNode, tokens: readonly Token[]): string | null {
  if (call.spaced || call.isMember || call.isDef || !call.balanced) return null;

  if (call.calleeName === 'can_msg_get') {
    const arg = call.args[1];
    return arg === undefined ? null : singleString(arg, tokens)?.value ?? null;
  }

  if (call.calleeName === 'can_msg_new') {
    const names: string[] = [];
    for (const arg of call.args) {
      const literal = singleString(arg, tokens);
      if (literal !== null) names.push(literal.value);
    }
    return names.length === 1 ? names[0] : null;
  }

  return null;
}

/* ---- Variable -> message tracking ---- */

const STATEMENT_KEYWORDS = new Set([
  'if', 'elif', 'else', 'while', 'for', 'def', 'end', 'class', 'break',
  'continue', 'return', 'var', 'do', 'import', 'as', 'try', 'except',
  'raise', 'static',
]);

function trackAssignments(
  source: string,
  tokens: readonly Token[],
  callAt: ReadonlyMap<number, CallNode>,
): Map<string, Set<string>> {
  const varToMsgs = new Map<string, Set<string>>();

  function isStatementBoundary(index: number): boolean {
    const next = tokens[index];
    if (next === undefined || next.kind === 'eof') return true;
    if (next.kind === 'punct') return next.text === ';';
    if (next.kind === 'keyword' && STATEMENT_KEYWORDS.has(next.text)) return true;

    const previous = tokens[index - 1];
    if (previous === undefined) return false;
    const gap = source.slice(previous.span.end, next.span.start);
    return gap.includes('\n') || gap.includes('\r');
  }

  for (let i = 1; i < tokens.length; i++) {
    const op = tokens[i];
    if (op.kind !== 'punct' || (op.text !== '=' && op.text !== ':=')) continue;

    const target = tokens[i - 1];
    if (target.kind !== 'id') continue;

    const before = i >= 2 ? tokens[i - 2] : undefined;
    if (before?.kind === 'punct' && (before.text === '.' || before.text === '[')) continue;

    let start = i + 1;
    let wrappers = 0;
    while (true) {
      const token = tokens[start];
      if (token?.kind !== 'punct' || token.text !== '(') break;
      start++;
      wrappers++;
    }

    const call = callAt.get(start);
    if (call === undefined || !call.balanced) continue;

    let after = call.rparenIndex + 1;
    let closed = 0;
    while (closed < wrappers) {
      const token = tokens[after];
      if (token?.kind !== 'punct' || token.text !== ')') break;
      after++;
      closed++;
    }
    if (closed !== wrappers || !isStatementBoundary(after)) continue;

    const message = messageNameFromCall(call, tokens);
    if (message === null) continue;

    let names = varToMsgs.get(target.text);
    if (names === undefined) {
      names = new Set();
      varToMsgs.set(target.text, names);
    }
    names.add(message);
  }

  return varToMsgs;
}

/** Scan source for `var x = can_msg_get(...)` / `can_msg_new(...)` bindings. */
export function trackVarToMsgs(source: string): Map<string, Set<string>> {
  const lexed = tokenize(source);
  const parsed = buildCalls(lexed.tokens);
  const callAt = new Map<number, CallNode>();
  for (const call of parsed.calls) callAt.set(call.nameTokenIndex, call);
  return trackAssignments(source, lexed.tokens, callAt);
}

/** Like trackVarToMsgs but only returns variables with exactly one message. */
export function buildVarToMsgMap(source: string): Map<string, string> {
  const map = new Map<string, string>();
  for (const [name, messages] of trackVarToMsgs(source)) {
    if (messages.size === 1) map.set(name, [...messages][0]);
  }
  return map;
}

/* ---- DBC-aware call dispatch ---- */

type DbcCall = 'can_msg_get' | 'can_msg_new' | 'msg_sig_get' | 'msg_sig_set';

interface RuleContext {
  tokens: readonly Token[];
  index: MessageIndex;
  callAt: ReadonlyMap<number, CallNode>;
  varToMsgs: Map<string, Set<string>>;
  renderArg: (arg: ArgRange) => string;
  warn: (code: string, message: string, span: Span) => void;
}

type Outcome =
  | { kind: 'render'; text: string }
  | { kind: 'pass' }
  | { kind: 'error'; code: string; message: string; span: Span };

type Handler = (call: CallNode, ctx: RuleContext) => Outcome;

function fail(code: string, message: string, span: Span): Outcome {
  return { kind: 'error', code, message, span };
}

function rewriteCanMsgGet(call: CallNode, ctx: RuleContext): Outcome {
  if (call.args.length !== 2) {
    return fail('dbc/arity', 'can_msg_get: expected 2 arguments', call.span);
  }

  const [bus, nameArg] = call.args;
  if (isEmpty(bus) || isEmpty(nameArg)) {
    return fail('dbc/missing-argument', 'can_msg_get: argument is missing', call.span);
  }

  const first = singleString(bus, ctx.tokens);
  if (first !== null) {
    return fail(
      'dbc/position',
      'can_msg_get: message name must be the second argument (bus first)',
      first.span,
    );
  }

  const name = singleString(nameArg, ctx.tokens);
  if (name !== null) {
    const message = ctx.index.byName.get(name.value);
    if (message === undefined) {
      return fail('dbc/unknown-message', `can_msg_get: message '${name.value}' not found`, name.span);
    }
    return {
      kind: 'render',
      text: `can_msg_get(${ctx.renderArg(bus)}, 0x${message.id.toString(16)})`,
    };
  }

  const computed = topLevelStringSpan(nameArg, ctx.tokens, ctx.callAt);
  if (computed !== null) {
    return fail(
      'dbc/not-literal',
      'can_msg_get: message name must be a compile-time string literal',
      computed,
    );
  }

  // Numeric id or a runtime expression: the runtime binding takes an int.
  return { kind: 'pass' };
}

function rewriteCanMsgNew(call: CallNode, ctx: RuleContext): Outcome {
  if (call.args.length === 0 || call.args.length > 2) {
    return fail('dbc/arity', 'can_msg_new: expected 1 or 2 arguments', call.span);
  }
  if (call.args.some(isEmpty)) {
    return fail('dbc/missing-argument', 'can_msg_new: argument is missing', call.span);
  }

  const literals: Array<Token & { kind: 'string' }> = [];
  let computed: Span | null = null;
  for (const arg of call.args) {
    const literal = singleString(arg, ctx.tokens);
    if (literal !== null) {
      literals.push(literal);
      continue;
    }
    const top = topLevelStringSpan(arg, ctx.tokens, ctx.callAt);
    if (top !== null && computed === null) computed = top;
  }

  if (computed !== null) {
    return fail(
      'dbc/not-literal',
      'can_msg_new: message name must be a compile-time string literal',
      computed,
    );
  }

  if (literals.length > 1) {
    return fail(
      'dbc/ambiguous-message',
      'can_msg_new: ambiguous message name (two string literals)',
      literals[1].span,
    );
  }

  if (literals.length === 1) {
    const literal = literals[0];
    const message = ctx.index.byName.get(literal.value);
    if (message === undefined) {
      return fail('dbc/unknown-message', `can_msg_new: message '${literal.value}' not found`, literal.span);
    }
    return {
      kind: 'render',
      text: `can_msg_new(0x${message.id.toString(16)}, ${message.dlc})`,
    };
  }

  // The binding requires exactly (id, dlc); one numeric argument cannot run.
  if (call.args.length === 1) {
    return fail('dbc/arity', 'can_msg_new: numeric form requires (id, dlc)', call.span);
  }

  return { kind: 'pass' };
}

type ResolvedSignal =
  | { ok: true; sig: Signal }
  | { ok: false; error: string };

function resolveSignal(
  owner: string | null,
  signalName: string,
  context: string,
  span: Span,
  ctx: RuleContext,
): ResolvedSignal {
  if (owner !== null) {
    const signal = ctx.index.byName.get(owner)?.signals.find(s => s.name === signalName) ?? null;
    if (signal !== null) return { ok: true, sig: signal };

    const owners = [...new Set(
      (ctx.index.bySignal.get(signalName) ?? []).map(m => m.msg.name),
    )].filter(name => name !== owner);

    if (owners.length === 1) {
      return {
        ok: false,
        error: `${context}: signal '${signalName}' belongs to '${owners[0]}', not '${owner}'`,
      };
    }
    if (owners.length > 1) {
      return {
        ok: false,
        error: `${context}: signal '${signalName}' is not in message '${owner}', but is present in multiple messages (${owners.join(', ')})`,
      };
    }
    return {
      ok: false,
      error: `${context}: signal '${signalName}' not found in message '${owner}'`,
    };
  }

  const matches = ctx.index.bySignal.get(signalName) ?? [];
  if (matches.length === 0) {
    return { ok: false, error: `${context}: signal '${signalName}' not found` };
  }

  const owners = [...new Set(matches.map(m => m.msg.name))];
  if (owners.length > 1) {
    ctx.warn(
      'dbc/ambiguous-signal',
      `${context}: signal '${signalName}' exists in multiple messages (${owners.join(', ')}); using '${matches[0].msg.name}'; assign the draft via can_msg_get/can_msg_new to disambiguate`,
      span,
    );
  }

  return { ok: true, sig: matches[0].sig };
}

function resolveDraftOwner(arg: ArgRange, ctx: RuleContext): string | null {
  const name = singleId(arg, ctx.tokens);
  if (name !== null) {
    const messages = ctx.varToMsgs.get(name);
    return messages !== undefined && messages.size === 1 ? [...messages][0] : null;
  }

  const call = soleCallInArg(arg, ctx.tokens, ctx.callAt);
  if (call !== null && (call.calleeName === 'can_msg_get' || call.calleeName === 'can_msg_new')) {
    return messageNameFromCall(call, ctx.tokens);
  }

  return null;
}

function rewriteSignalCall(
  kind: 'msg_sig_get' | 'msg_sig_set',
  call: CallNode,
  ctx: RuleContext,
): Outcome {
  const expected = kind === 'msg_sig_get' ? 2 : 3;
  const inline = kind === 'msg_sig_get' ? 7 : 8;

  if (call.args.length === inline) return { kind: 'pass' };
  if (call.args.length !== expected) {
    return fail(
      'dbc/arity',
      `${kind}: expected ${expected} arguments (or ${inline} for the inline form)`,
      call.span,
    );
  }
  if (call.args.some(isEmpty)) {
    return fail('dbc/missing-argument', `${kind}: argument is missing`, call.span);
  }

  const signal = singleString(call.args[1], ctx.tokens);
  if (signal === null) {
    return fail(
      'dbc/not-literal',
      `${kind}: signal name must be a compile-time string literal`,
      hasString(call.args[1], ctx.tokens)
        ? firstStringSpan(call.args[1], ctx.tokens)
        : call.args[1].span,
    );
  }

  // Render nested calls before resolving so their diagnostics are collected
  // even when the outer signal turns out to be wrong.
  const draft = ctx.renderArg(call.args[0]);
  const value = kind === 'msg_sig_set' ? ctx.renderArg(call.args[2]) : '';

  const owner = resolveDraftOwner(call.args[0], ctx);
  const resolved = resolveSignal(owner, signal.value, kind, signal.span, ctx);
  if (!resolved.ok) {
    return fail('dbc/signal', resolved.error, signal.span);
  }

  const meta = sigMeta(resolved.sig);
  return {
    kind: 'render',
    text: kind === 'msg_sig_get'
      ? `msg_sig_get(${draft}, ${meta})`
      : `msg_sig_set(${draft}, ${meta}, ${value})`,
  };
}

const RULES: Record<DbcCall, Handler> = {
  can_msg_get: rewriteCanMsgGet,
  can_msg_new: rewriteCanMsgNew,
  msg_sig_get: (call, ctx) => rewriteSignalCall('msg_sig_get', call, ctx),
  msg_sig_set: (call, ctx) => rewriteSignalCall('msg_sig_set', call, ctx),
};

function isDbcCall(name: string): name is DbcCall {
  return Object.hasOwn(RULES, name);
}

/* ---- Main entry point ---- */

export function preprocessScript(
  sourceCode: string,
  messages: Message[],
): PreprocessResult {
  const lexed = tokenize(sourceCode);
  const parsed: ParseResult = buildCalls(lexed.tokens);
  const bag = new DiagnosticBag();

  for (const error of lexed.errors) bag.error(error.code, error.message, error.span);
  for (const error of parsed.errors) bag.error(error.code, error.message, error.span);

  const index = getMessageIndex(messages);
  const callAt = new Map<number, CallNode>();
  for (const call of parsed.calls) callAt.set(call.nameTokenIndex, call);
  const varToMsgs = trackAssignments(sourceCode, lexed.tokens, callAt);

  function rewrite(call: CallNode, renderArg: (arg: ArgRange) => string): string | null {
    if (call.isMember || call.isDef || !call.balanced) return null;

    if (call.spaced) {
      if (!isDbcCall(call.calleeName)) return null;
      const message = `${call.calleeName}: call parenthesis must follow the name immediately`;
      bag.error('dbc/space-before-call', message, call.span);
      return berryFailure(message);
    }

    if (!isDbcCall(call.calleeName)) return null;

    const ctx: RuleContext = {
      tokens: lexed.tokens,
      index,
      callAt,
      varToMsgs,
      renderArg,
      warn: (code, message, span) => bag.warn(code, message, span),
    };

    const outcome = RULES[call.calleeName](call, ctx);
    if (outcome.kind === 'pass') return null;
    if (outcome.kind === 'render') return outcome.text;

    bag.error(outcome.code, outcome.message, outcome.span);
    return berryFailure(outcome.message);
  }

  const code = render(sourceCode, lexed.tokens, callAt, rewrite);
  bag.errors.sort((a, b) => a.span.start - b.span.start);
  bag.warnings.sort((a, b) => a.span.start - b.span.start);

  return { code, errors: bag.errors, warnings: bag.warnings };
}
