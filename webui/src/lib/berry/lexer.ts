import type { Diagnostic, Span } from './diagnostic.ts';
import { KEYWORDS, PUNCTUATION, SIMPLE_ESCAPES } from './lexer-tables.ts';

/**
 * Berry tokenizer.
 *
 * Mirrors the lexical rules in firmware/components/berry/src/be_lexer.c:
 * `#` line comments, `#- ... -#` block comments, both quote styles, the full
 * escape set, adjacent string literals, and the `OptCallLBK` / `OptSpaceLBK`
 * distinction for `(`. Comments and whitespace are skipped; their bytes live
 * in the gaps between token spans, so rendering can copy them verbatim.
 *
 * The keyword, punctuation, and simple-escape tables are generated from the
 * vendored C lexer by webui/scripts/gen-lexer-tables.ts. The token stream is
 * checked against the C lexer by webui/tests/lexer-conformance.test.ts. A Berry
 * bump fails the source-lock test until the tables are regenerated.
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

  function stringToken(
    start: number,
    end: number,
    atLine: number,
    atColumn: number,
    value: string,
  ): Token {
    return {
      kind: 'string',
      text: source.slice(start, end),
      value,
      span: span(start, end, atLine, atColumn),
    };
  }

  function unfinishedString(start: number, atLine: number, atColumn: number, value: string): Token {
    fail('lex/unfinished-string', 'unfinished string', start, i, atLine, atColumn);
    return stringToken(start, i, atLine, atColumn, value);
  }

  /** Decode one escape at the backslash. Returns null when the string cannot continue. */
  function decodeEscape(): string | null {
    const escapeStart = i;
    const escapeLine = line;
    const escapeColumn = column(i);
    i++;

    if (i >= length) return null;
    const e = source[i];
    if (e === '\n' || e === '\r') return null;

    const simple = SIMPLE_ESCAPES[e];
    if (simple !== undefined) {
      i++;
      return simple;
    }

    if (e === 'x') {
      const digits = source.slice(i + 1, i + 3);
      if (digits.length === 2 && isHex(digits[0]) && isHex(digits[1])) {
        i += 3;
        return String.fromCharCode(parseInt(digits, 16));
      }
      fail('lex/invalid-hex', 'invalid hexadecimal escape (two hex digits required)',
        escapeStart, Math.min(i + 2, length), escapeLine, escapeColumn);
      i++;
      return '';
    }

    if (e === 'u') {
      const digits = source.slice(i + 1, i + 5);
      if (digits.length === 4 && [...digits].every(isHex)) {
        i += 5;
        return String.fromCharCode(parseInt(digits, 16));
      }
      fail('lex/invalid-unicode', 'invalid unicode escape (four hex digits required)',
        escapeStart, Math.min(i + 4, length), escapeLine, escapeColumn);
      i++;
      return '';
    }

    if (isOctal(e)) {
      const digits = source.slice(i, i + 3);
      if (digits.length === 3 && [...digits].every(isOctal)) {
        i += 3;
        return String.fromCharCode(parseInt(digits, 8) & 0xff);
      }
      fail('lex/invalid-octal', 'invalid octal escape (three octal digits required)',
        escapeStart, Math.min(i + 2, length), escapeLine, escapeColumn);
      i++;
      return '';
    }

    fail('lex/unknown-escape', `unknown escape sequence '\\${e}'`,
      escapeStart, i + 1, escapeLine, escapeColumn);
    i++;
    return '';
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
          return unfinishedString(start, atLine, atColumn, value);
        }

        if (c === '\\') {
          const decoded = decodeEscape();
          if (decoded === null) {
            return unfinishedString(start, atLine, atColumn, value);
          }
          value += decoded;
          continue;
        }

        value += c;
        i++;
      }

      if (!closed) {
        return unfinishedString(start, atLine, atColumn, value);
      }

      // Berry's scan_string consumes the delimiters after a literal and keeps
      // going when another quote follows, so `"a" "b"` is one token.
      skipTrivia();
      if (i < length && (source[i] === '"' || source[i] === "'")) continue;

      return stringToken(start, i, atLine, atColumn, value);
    }
  }

  function scanNumber(): Token {
    const start = i;
    const atLine = line;
    const atColumn = column(i);
    const startsWithDigit = isDigit(source[i]);

    if (source[i] === '0' && (source[i + 1] === 'x' || source[i + 1] === 'X')) {
      i += 2;
      const digits = i;
      while (i < length && isHex(source[i])) i++;
      if (i === digits) {
        fail('lex/malformed-number', 'invalid hexadecimal number', start, i, atLine, atColumn);
      }
    } else {
      while (i < length && isDigit(source[i])) i++;
      // Berry's decimal_dots consumes one dot unless it starts `..`, even
      // when no digit follows, so `1.` is a real and `1..2` is `1 .. 2`.
      if (i < length && source[i] === '.' && source[i + 1] !== '.') {
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

    // Berry checks a trailing identifier or dot only for numbers that start
    // with a digit. A leading-dot real (`.5`) skips the check. It reports a
    // trailing identifier without consuming it, and consumes a second
    // decimal dot before reporting.
    if (startsWithDigit && i < length && isIdentStart(source[i])) {
      fail('lex/malformed-number', 'malformed number', start, i, atLine, atColumn);
    } else if (startsWithDigit && i < length && source[i] === '.' && source[i + 1] !== '.') {
      i++;
      while (i < length && isDigit(source[i])) i++;
      fail('lex/malformed-number', 'malformed number', start, i, atLine, atColumn);
    }

    return {
      kind: 'number',
      text: source.slice(start, i),
      span: span(start, i, atLine, atColumn),
    };
  }

  while (true) {
    const skipped = skipTrivia();

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
        callable: op === '(' && !skipped,
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
