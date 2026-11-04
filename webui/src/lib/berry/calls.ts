import type { Diagnostic, Span } from './diagnostic.ts';
import type { Token } from './lexer.ts';

/** One call argument, including the raw ranges on both sides of its tokens. */
export interface ArgRange {
  /** First token index after stripping outer grouping parentheses; -1 when empty. */
  first: number;
  /** Last token index after stripping outer grouping parentheses; -1 when empty. */
  last: number;
  /** Source range between the argument's delimiters, including trivia. */
  span: Span;
}

export interface CallNode {
  /** Index of the callee identifier token. */
  nameTokenIndex: number;
  /** Callee text. Always a plain identifier for the nodes this module emits. */
  calleeName: string;
  /** Index of the opening `(` token. */
  lparenIndex: number;
  /** Index of the matching `)`, or -1 when the parentheses are unbalanced. */
  rparenIndex: number;
  /** Top-level arguments. Empty for `foo()` and for an unbalanced call. */
  args: ArgRange[];
  /** The callee is preceded by `.`. */
  isMember: boolean;
  /** The callee is preceded by the `def` keyword. */
  isDef: boolean;
  /** Berry lexed this `(` as grouping because whitespace or a comment precedes it. */
  spaced: boolean;
  balanced: boolean;
  /** Callee start through the closing `)`, or through the `(` when unbalanced. */
  span: Span;
}

export interface ParseResult {
  calls: CallNode[];
  errors: Diagnostic[];
}

export type Rewriter = (
  call: CallNode,
  renderArg: (arg: ArgRange) => string,
) => string | null;

type OpenToken = Token & { kind: 'punct'; text: '(' | '[' | '{' };
type CloseToken = Token & { kind: 'punct'; text: ')' | ']' | '}' };

function isOpen(token: Token): token is OpenToken {
  return token.kind === 'punct' && (token.text === '(' || token.text === '[' || token.text === '{');
}

function isClose(token: Token): token is CloseToken {
  return token.kind === 'punct' && (token.text === ')' || token.text === ']' || token.text === '}');
}

function closerFor(token: Token): string {
  if (token.kind !== 'punct') return '';
  if (token.text === '(') return ')';
  if (token.text === '[') return ']';
  if (token.text === '{') return '}';
  return '';
}

/** Match every bracket pair and report the ones that do not pair. */
function matchBrackets(tokens: readonly Token[], errors: Diagnostic[]): Map<number, number> {
  const match = new Map<number, number>();
  const stack: Array<{ index: number; token: OpenToken }> = [];

  for (let i = 0; i < tokens.length; i++) {
    const token = tokens[i];
    if (isOpen(token)) {
      stack.push({ index: i, token });
      continue;
    }
    if (!isClose(token)) continue;

    const top = stack.at(-1);
    if (top !== undefined && closerFor(top.token) === token.text) {
      stack.pop();
      match.set(top.index, i);
      match.set(i, top.index);
    } else {
      errors.push({
        code: 'parse/unmatched-close',
        message: `unmatched '${token.text}'`,
        span: token.span,
      });
    }
  }

  for (const { token } of stack) {
    errors.push({
      code: 'parse/unclosed',
      message: `unclosed '${token.text}'`,
      span: token.span,
    });
  }

  errors.sort((a, b) => a.span.start - b.span.start);
  return match;
}

/** Token range after stripping balanced outer grouping parentheses. */
function innerRange(
  first: number,
  last: number,
  tokens: readonly Token[],
  match: ReadonlyMap<number, number>,
): { first: number; last: number } {
  while (first < last) {
    const open = tokens[first];
    const close = tokens[last];
    if (
      open.kind !== 'punct' || open.text !== '('
      || close.kind !== 'punct' || close.text !== ')'
      || match.get(first) !== last
    ) break;
    first++;
    last--;
  }
  return first <= last ? { first, last } : { first: -1, last: -1 };
}

function splitArgs(
  tokens: readonly Token[],
  lparen: number,
  rparen: number,
  match: ReadonlyMap<number, number>,
): ArgRange[] {
  if (lparen + 1 >= rparen) return [];

  const args: ArgRange[] = [];
  let start = lparen + 1;
  let i = start;

  const push = (first: number, delimiterIndex: number, toOffset: number): void => {
    const last = i - 1;
    const inner = innerRange(first, last, tokens, match);
    const delimiter = tokens[delimiterIndex];
    const span: Span = {
      start: delimiter.span.end,
      end: toOffset,
      line: delimiter.span.line,
      column: delimiter.span.column + (delimiter.span.end - delimiter.span.start),
    };
    args.push({ first: inner.first, last: inner.last, span });
  };

  while (i < rparen) {
    const token = tokens[i];
    if (isOpen(token)) {
      const closing = match.get(i);
      if (closing !== undefined && closing < rparen) {
        i = closing + 1;
        continue;
      }
    }
    if (token.kind === 'punct' && token.text === ',') {
      push(start, start - 1, token.span.start);
      start = i + 1;
    }
    i++;
  }

  push(start, start - 1, tokens[rparen].span.start);
  return args;
}

/** Find call candidates, declarations, grouping forms, and bracket errors. */
export function buildCalls(tokens: readonly Token[]): ParseResult {
  const errors: Diagnostic[] = [];
  const match = matchBrackets(tokens, errors);
  const calls: CallNode[] = [];

  for (let i = 0; i < tokens.length; i++) {
    const token = tokens[i];
    if (token.kind !== 'id') continue;

    const next = tokens[i + 1];
    if (
      next === undefined
      || next.kind !== 'punct'
      || next.text !== '('
    ) {
      continue;
    }

    const previous = i > 0 ? tokens[i - 1] : undefined;
    const isMember = previous?.kind === 'punct' && previous.text === '.';
    const isDef = previous?.kind === 'keyword' && previous.text === 'def';
    const spaced = !next.callable;
    const rparenIndex = match.get(i + 1) ?? -1;
    const balanced = rparenIndex >= 0;
    const span: Span = balanced
      ? {
        start: token.span.start,
        end: tokens[rparenIndex].span.end,
        line: token.span.line,
        column: token.span.column,
      }
      : {
        start: token.span.start,
        end: next.span.end,
        line: token.span.line,
        column: token.span.column,
      };

    calls.push({
      nameTokenIndex: i,
      calleeName: token.text,
      lparenIndex: i + 1,
      rparenIndex,
      args: balanced ? splitArgs(tokens, i + 1, rparenIndex, match) : [],
      isMember,
      isDef,
      spaced,
      balanced,
      span,
    });
  }

  return { calls, errors };
}

/**
 * Render the whole source. A rewriter returns a replacement for a call, or
 * null to leave the call in place and keep scanning inside it. Everything not
 * replaced is copied from the source, so comments, whitespace, and escapes are
 * preserved byte for byte.
 */
export function render(
  source: string,
  tokens: readonly Token[],
  callAt: ReadonlyMap<number, CallNode>,
  rewriter: Rewriter,
): string {
  if (tokens.length === 0) return source;

  // Replacement text trims leading trivia but keeps trailing trivia, so a
  // value that ends in a line comment still leaves the generated `)` on a
  // fresh line. The generated separators supply the leading space.
  function renderArg(arg: ArgRange): string {
    if (arg.first < 0 || arg.last < arg.first) return '';
    return renderTokens(arg.first, arg.last, tokens[arg.first].span.start, arg.span.end);
  }

  function renderTokens(fromIndex: number, toIndex: number, fromOffset: number, toOffset: number): string {
    let out = '';
    let cursor = fromOffset;
    let i = fromIndex;

    while (i <= toIndex) {
      const token = tokens[i];
      out += source.slice(cursor, token.span.start);

      const call = callAt.get(i);
      if (call !== undefined && call.balanced) {
        const replacement = rewriter(call, renderArg);
        if (replacement !== null) {
          out += replacement;
          cursor = call.span.end;
          i = call.rparenIndex + 1;
          continue;
        }
      }

      out += source.slice(token.span.start, token.span.end);
      cursor = token.span.end;
      i++;
    }

    out += source.slice(cursor, toOffset);
    return out;
  }

  return renderTokens(0, tokens.length - 1, 0, source.length);
}
