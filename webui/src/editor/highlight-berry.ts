/*
 * Shared Berry syntax highlighting.
 *
 * Both the CodeMirror editor (via StreamLanguage) and the ScriptingGuide
 * overlay (via marked renderer) need to tokenize Berry code.  This file is
 * the single source of truth for the tokenizer logic and highlight colors.
 */
import { StreamLanguage, type StreamParser } from '@codemirror/language';
import { tags } from '@lezer/highlight';
import { KEYWORD_SET, BUILTINS } from './autocomplete';

/* ── Token colors (matches the CodeMirror warm theme) ── */
export const BERRY_COLORS = {
  comment: '#70583a',
  keyword: '#c97a4a',
  builtin: '#b8c47a',
  string: '#ffa24a',
  number: '#d68a3a',
  variable: '#f4d9a1',
  operator: '#e6c07a',
  bracket: '#a08555',
} as const;

const IDENT = /^[A-Za-z_][\w]*/;

/* ── StreamLanguage parser for the CodeMirror editor ── */
export const berryParser: StreamParser<unknown> = {
  name: 'berry',
  languageData: { commentTokens: { line: '#' } },
  token(stream) {
    if (stream.eatSpace()) return null;
    if (stream.match(/#.*$/)) return 'comment';
    if (stream.match(/"([^"\\]|\\.)*"/) || stream.match(/'([^'\\]|\\.)*'/)) return 'string';
    if (stream.match(/0x[0-9A-Fa-f]+/) || stream.match(/\d+(\.\d+)?/)) return 'number';
    if (stream.match(/[{}()[\]]/)) return 'bracket';
    if (stream.match(/[<>!=+\-*/%&|^~]+/)) return 'operator';

    const word = stream.match(IDENT);
    if (word) {
      const value = stream.current();
      if (KEYWORD_SET.has(value)) return 'keyword';
      if (BUILTINS.has(value)) return 'builtin';
      return 'variableName';
    }

    stream.next();
    return null;
  },
};

/** Create a CodeMirror `Language` object for Berry. */
export const berryLanguage = StreamLanguage.define(berryParser);

/* ── Static HTML highlighter (no CodeMirror dependency) ── */

function esc(s: string): string {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

/**
 * Tokenize Berry source text and return HTML with inline-colored `<span>`s
 * that match the CodeMirror warm theme.  No external dependencies needed.
 */
export function highlightBerryToHtml(code: string): string {
  const out: string[] = [];
  let i = 0;
  while (i < code.length) {
    // whitespace
    const wsMatch = code.slice(i).match(/^[ \t\r\n]+/);
    if (wsMatch) { out.push(wsMatch[0]); i += wsMatch[0].length; continue; }
    // comments
    if (code[i] === '#') {
      const end = code.indexOf('\n', i);
      const comment = code.slice(i, end === -1 ? undefined : end);
      out.push('<span style="color:' + BERRY_COLORS.comment + '">' + esc(comment) + '</span>');
      i += comment.length;
      continue;
    }
    // strings
    if (code[i] === '"' || code[i] === "'") {
      const q = code[i];
      let j = i + 1;
      while (j < code.length && code[j] !== q) {
        if (code[j] === '\\') j++;
        j++;
      }
      if (j < code.length) j++; // closing quote
      out.push('<span style="color:' + BERRY_COLORS.string + '">' + esc(code.slice(i, j)) + '</span>');
      i = j;
      continue;
    }
    // hex numbers
    const hexMatch = code.slice(i).match(/^0x[0-9A-Fa-f]+/);
    if (hexMatch) {
      out.push('<span style="color:' + BERRY_COLORS.number + '">' + hexMatch[0] + '</span>');
      i += hexMatch[0].length;
      continue;
    }
    // decimal numbers
    const decMatch = code.slice(i).match(/^\d+(\.\d+)?/);
    if (decMatch) {
      out.push('<span style="color:' + BERRY_COLORS.number + '">' + decMatch[0] + '</span>');
      i += decMatch[0].length;
      continue;
    }
    // brackets
    if (/[{}()[\]]/.test(code[i])) {
      out.push('<span style="color:' + BERRY_COLORS.bracket + '">' + code[i] + '</span>');
      i++;
      continue;
    }
    // operators
    const opMatch = code.slice(i).match(/^[<>!=+\-*/%&|^~]+/);
    if (opMatch) {
      out.push('<span style="color:' + BERRY_COLORS.operator + '">' + esc(opMatch[0]) + '</span>');
      i += opMatch[0].length;
      continue;
    }
    // words
    const wordMatch = code.slice(i).match(IDENT);
    if (wordMatch) {
      const word = wordMatch[0];
      if (KEYWORD_SET.has(word))
        out.push('<span style="color:' + BERRY_COLORS.keyword + ';font-weight:600">' + word + '</span>');
      else if (BUILTINS.has(word))
        out.push('<span style="color:' + BERRY_COLORS.builtin + '">' + word + '</span>');
      else
        out.push('<span style="color:' + BERRY_COLORS.variable + '">' + word + '</span>');
      i += word.length;
      continue;
    }
    // fallback
    out.push(esc(code[i]));
    i++;
  }
  return out.join('');
}
