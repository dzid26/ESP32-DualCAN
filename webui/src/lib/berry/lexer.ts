import type { Diagnostic, Span } from './diagnostic.ts';

/**
 * Berry tokenizer.
 *
 * Mirrors the lexical rules in firmware/components/berry/src/be_lexer.c:
 * `#` line comments, `#- ... -#` block comments, both quote styles, the full
 * escape set, adjacent string literals, and the `OptCallLBK` / `OptSpaceLBK`
 * distinction for `(`. Comments and whitespace are skipped; their bytes live
 * in the gaps between token spans, so rendering can copy them verbatim.
 */
export type Token =
  | { kind: 'id'; text: string; span: Span }
  | { kind: 'keyword'; text: string; span: Span }
  | { kind: 'number'; text: string; span: Span }
  | { kind: 'string'; text: string; value: string; span: Span }
  | { kind: 'punct'; text: string; callable: boolean; span: Span }
  | { kind: 'eof'; span: Span };

export interface LexResult {
  tokens: Token[];
  errors: Diagnostic[];
}

const KEYWORDS = new Set([
  'if', 'elif', 'else', 'while', 'for', 'def', 'end', 'class', 'break',
  'continue', 'return', 'true', 'false', 'nil', 'var', 'do', 'import',
  'as', 'try', 'except', 'raise', 'static',
]);

/* Longest first, so `..` wins over `.` and `:=` over `:`. */
const PUNCTUATION = [
  '<<=', '>>=', '..', '&&', '||', '==', '!=', '<=', '>=', '<<', '>>',
  '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=', '->', ':=',
  '+', '-', '*', '/', '%', '<', '>', '&', '|', '^', '~', '!', '(',
  ')', '[', ']', '{', '}', '.', ',', ';', ':', '?', '=',
].sort((a, b) => b.length - a.length);

const SIMPLE_ESCAPES: Record<string, string> = {
  a: '\x07', b: '\b', f: '\f', n: '\n', r: '\r', t: '\t', v: '\v',
  '\\': '\\', "'": "'", '"': '"', '?': '?',
};

function isIdentStart(c: string): boolean {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c === '_';
}

function isWord(c: string): boolean {
  return isIdentStart(c) || (c >= '0' && c <= '9');
}

function isDigit(c: string): boolean {
  return c >= '0' && c <= '9';
}

function isHex(c: string): boolean {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

function isOctal(c: string): boolean {
  return c >= '0' && c <= '7';
}

function hexValue(c: string): number {
  if (c >= '0' && c <= '9') return c.charCodeAt(0) - 48;
  if (c >= 'a' && c <= 'f') return c.charCodeAt(0) - 87;
  return c.charCodeAt(0) - 55;
}

export function tokenize(source: string): LexResult {
  const tokens: Token[] = [];
  const errors: Diagnostic[] = [];
  const length = source.length;
  let i = 0;
  let line = 1;
  let lineStart = 0;

  const column = (offset: number): number => offset - lineStart + 1;
  const span = (start: number, end: number, atLine: number, atColumn: number): Span =>
    ({ start, end, line: atLine, column: atColumn });
  const fail = (
    code: string,
    message: string,
    start: number,
    end: number,
    atLine: number,
    atColumn: number,
  ): void => {
    errors.push({ code, message, span: span(start, end, atLine, atColumn) });
  };

  /** Move to `end`, counting every newline pair on the way. */
  function advanceTo(end: number): void {
    while (i < end) {
      const c = source[i];
      if (c === '\n' || c === '\r') {
        const pair = c === '\n' ? '\r' : '\n';
        i++;
        if (i < end && source[i] === pair) i++;
        line++;
        lineStart = i;
      } else {
        i++;
      }
    }
  }

  /** Skip whitespace and comments. Returns true when anything was skipped. */
  function skipTrivia(): boolean {
    let skipped = false;
    while (i < length) {
      const c = source[i];
      if (c === ' ' || c === '\t' || c === '\f' || c === '\v') {
        i++;
        skipped = true;
        continue;
      }
      if (c === '\n' || c === '\r') {
        const pair = c === '\n' ? '\r' : '\n';
        i++;
        if (i < length && source[i] === pair) i++;
        line++;
        lineStart = i;
        skipped = true;
        continue;
      }
      if (c === '#') {
        const start = i;
        const atLine = line;
        const atColumn = column(i);
        skipped = true;
        if (source[i + 1] === '-') {
          const end = source.indexOf('-#', i + 2);
          if (end < 0) {
            fail('lex/unterminated-comment', 'unterminated block comment', start, length, atLine, atColumn);
            advanceTo(length);
            return true;
          }
          advanceTo(end + 2);
        } else {
          let end = i;
          while (end < length && source[end] !== '\n' && source[end] !== '\r') end++;
          advanceTo(end);
        }
        continue;
      }
      break;
    }
    return skipped;
  }

  /** Scan one or more adjacent string literals. Leaves `i` after the last one. */
  function scanString(): Token {
    const start = i;
    const atLine = line;
    const atColumn = column(i);
    let value = '';

    while (true) {
      const quote = source[i];
      i++;
      let closed = false;

      while (i < length) {
        const c = source[i];

        if (c === quote) {
          i++;
          closed = true;
          break;
        }

        if (c === '\n' || c === '\r') {
          fail('lex/unfinished-string', 'unfinished string', start, i, atLine, atColumn);
          return {
            kind: 'string',
            text: source.slice(start, i),
            value,
            span: span(start, i, atLine, atColumn),
          };
        }

        if (c === '\\') {
          const escapeStart = i;
          const escapeLine = line;
          const escapeColumn = column(i);
          i++;

          if (i >= length) {
            fail('lex/unfinished-string', 'unfinished string', start, length, atLine, atColumn);
            return {
              kind: 'string',
              text: source.slice(start, length),
              value,
              span: span(start, length, atLine, atColumn),
            };
          }

          const e = source[i];
          if (e === '\n' || e === '\r') {
            fail('lex/unfinished-string', 'unfinished string', start, i, atLine, atColumn);
            return {
              kind: 'string',
              text: source.slice(start, i),
              value,
              span: span(start, i, atLine, atColumn),
            };
          }

          const simple = SIMPLE_ESCAPES[e];
          if (simple !== undefined) {
            value += simple;
            i++;
            continue;
          }

          if (e === 'x') {
            if (i + 2 < length && isHex(source[i + 1]) && isHex(source[i + 2])) {
              value += String.fromCharCode(
                (hexValue(source[i + 1]) << 4) | hexValue(source[i + 2]),
              );
              i += 3;
            } else {
              fail('lex/invalid-hex', 'invalid hexadecimal escape (two hex digits required)',
                escapeStart, Math.min(i + 2, length), escapeLine, escapeColumn);
              i++;
            }
            continue;
          }

          if (e === 'u') {
            if (
              i + 4 < length
              && isHex(source[i + 1]) && isHex(source[i + 2])
              && isHex(source[i + 3]) && isHex(source[i + 4])
            ) {
              const code = (hexValue(source[i + 1]) << 12)
                | (hexValue(source[i + 2]) << 8)
                | (hexValue(source[i + 3]) << 4)
                | hexValue(source[i + 4]);
              value += String.fromCharCode(code);
              i += 5;
            } else {
              fail('lex/invalid-unicode', 'invalid unicode escape (four hex digits required)',
                escapeStart, Math.min(i + 4, length), escapeLine, escapeColumn);
              i++;
            }
            continue;
          }

          if (isOctal(e)) {
            if (
              i + 2 < length
              && isOctal(source[i + 1]) && isOctal(source[i + 2])
            ) {
              const code = ((source.charCodeAt(i) - 48) << 6)
                | ((source.charCodeAt(i + 1) - 48) << 3)
                | (source.charCodeAt(i + 2) - 48);
              value += String.fromCharCode(code & 0xff);
              i += 3;
            } else {
              fail('lex/invalid-octal', 'invalid octal escape (three octal digits required)',
                escapeStart, Math.min(i + 2, length), escapeLine, escapeColumn);
              i++;
            }
            continue;
          }

          fail('lex/unknown-escape', `unknown escape sequence '\\${e}'`,
            escapeStart, i + 1, escapeLine, escapeColumn);
          i++;
          continue;
        }

        value += c;
        i++;
      }

      if (!closed) {
        fail('lex/unfinished-string', 'unfinished string', start, length, atLine, atColumn);
        return {
          kind: 'string',
          text: source.slice(start, length),
          value,
          span: span(start, length, atLine, atColumn),
        };
      }

      // Berry's scan_string consumes the delimiters after a literal and keeps
      // going when another quote follows, so `"a" "b"` is one token.
      skipTrivia();
      if (i < length && (source[i] === '"' || source[i] === "'")) continue;

      return {
        kind: 'string',
        text: source.slice(start, i),
        value,
        span: span(start, i, atLine, atColumn),
      };
    }
  }

  function scanNumber(): Token {
    const start = i;
    const atLine = line;
    const atColumn = column(i);

    if (source[i] === '0' && (source[i + 1] === 'x' || source[i + 1] === 'X')) {
      i += 2;
      const digits = i;
      while (i < length && isHex(source[i])) i++;
      if (i === digits) {
        fail('lex/malformed-number', 'invalid hexadecimal number', start, i, atLine, atColumn);
      }
    } else {
      while (i < length && isDigit(source[i])) i++;
      if (i < length && source[i] === '.' && isDigit(source[i + 1])) {
        i++;
        while (i < length && isDigit(source[i])) i++;
      }
      if (i < length && (source[i] === 'e' || source[i] === 'E')) {
        i++;
        if (i < length && (source[i] === '+' || source[i] === '-')) i++;
        const digits = i;
        while (i < length && isDigit(source[i])) i++;
        if (i === digits) {
          fail('lex/malformed-number', 'malformed number', start, i, atLine, atColumn);
        }
      }
    }

    if (i < length && isIdentStart(source[i])) {
      while (i < length && isWord(source[i])) i++;
      fail('lex/malformed-number', 'malformed number', start, i, atLine, atColumn);
    }

    return {
      kind: 'number',
      text: source.slice(start, i),
      span: span(start, i, atLine, atColumn),
    };
  }

  let first = true;
  while (true) {
    const skipped = skipTrivia();
    const hadGap = first || skipped;
    first = false;

    if (i >= length) {
      tokens.push({ kind: 'eof', span: span(length, length, line, column(length)) });
      break;
    }

    const start = i;
    const atLine = line;
    const atColumn = column(i);
    const c = source[i];

    if (isIdentStart(c)) {
      i++;
      while (i < length && isWord(source[i])) i++;
      const text = source.slice(start, i);
      tokens.push({
        kind: KEYWORDS.has(text) ? 'keyword' : 'id',
        text,
        span: span(start, i, atLine, atColumn),
      });
      continue;
    }

    if (isDigit(c) || (c === '.' && isDigit(source[i + 1]))) {
      tokens.push(scanNumber());
      continue;
    }

    if (c === '"' || c === "'") {
      tokens.push(scanString());
      continue;
    }

    const op = PUNCTUATION.find(candidate => source.startsWith(candidate, i));
    if (op !== undefined) {
      tokens.push({
        kind: 'punct',
        text: op,
        callable: op === '(' && !hadGap,
        span: span(start, start + op.length, atLine, atColumn),
      });
      i += op.length;
      continue;
    }

    fail('lex/stray-character', `stray '${c}'`, start, start + 1, atLine, atColumn);
    i++;
  }

  return { tokens, errors };
}
