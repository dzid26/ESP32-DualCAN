/*
 * CodeMirror 6 editor setup.
 *
 * The web UI uses CodeMirror's core packages directly rather than an editor
 * wrapper. This module owns the custom Berry stream tokenizer, the warm
 * theme shared with app.css, and the extension factory.
 */
import { autocompletion, closeBrackets } from '@codemirror/autocomplete';
import { bracketMatching, defaultHighlightStyle, indentOnInput, indentUnit, syntaxHighlighting, HighlightStyle, StreamLanguage, type StreamParser } from '@codemirror/language';
import { highlightSelectionMatches, searchKeymap } from '@codemirror/search';
import { EditorState, type Extension } from '@codemirror/state';
import { drawSelection, dropCursor, EditorView, keymap, lineNumbers, highlightActiveLine, highlightActiveLineGutter } from '@codemirror/view';
import { tags } from '@lezer/highlight';
import { completeBerry, berryHover, BUILTINS, KEYWORD_SET } from './berry-completions';

const IDENT = /[A-Za-z_][\w]*/;

const berryParser: StreamParser<unknown> = {
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

const berryLanguage = StreamLanguage.define(berryParser);

const berryHighlight = HighlightStyle.define([
  { tag: tags.comment, color: '#70583a', fontStyle: 'italic' },
  { tag: tags.keyword, color: '#c97a4a' },
  { tag: tags.standard(tags.variableName), color: '#b8c47a' },
  { tag: tags.string, color: '#ffa24a' },
  { tag: tags.number, color: '#d68a3a' },
  { tag: tags.variableName, color: '#f4d9a1' },
  { tag: tags.operator, color: '#e6c07a' },
  { tag: tags.bracket, color: '#a08555' },
]);

const warmTheme = EditorView.theme({
  '&': {
    height: '100%',
    backgroundColor: '#18110a',
    color: '#f4d9a1',
  },
  '&.cm-focused': {
    outline: 'none',
  },
  '.cm-scroller': {
    fontFamily: "'Fira Code', 'Cascadia Code', monospace",
    fontSize: '13px',
    lineHeight: '1.5',
    overflow: 'auto',
  },
  '.cm-content': {
    caretColor: '#e07b1f',
    padding: '8px 0',
    /* No line wrapping — long lines trigger horizontal scrollbar on .cm-scroller */
    whiteSpace: 'pre',
  },
  '.cm-line': {
    padding: '0 10px',
  },
  '.cm-cursor, .cm-dropCursor': {
    borderLeftColor: '#e07b1f',
  },
  '.cm-selectionBackground, &.cm-focused .cm-selectionBackground': {
    backgroundColor: '#e07c1f64',
    outline: '1px solid #ffffff2b',
  },
  '.cm-content ::selection': {
    backgroundColor: '#e07c1f64',
  },
  '.cm-selectionMatch': {
    backgroundColor: '#e4dad222',
  },
  '.cm-activeLine': {
    backgroundColor: '#6d4c2829',
  },
  '.cm-gutters': {
    backgroundColor: '#18110a',
    color: '#70583a',
    borderRight: '1px solid #332514',
  },
  '.cm-activeLineGutter': {
    backgroundColor: '#2a1f10',
    color: '#e6c07a',
  },
  '.cm-matchingBracket, .cm-nonmatchingBracket': {
    backgroundColor: 'transparent',
    outline: 'none',
    fontWeight: '700',
  },
  '.cm-tooltip': {
    backgroundColor: '#362712',
    border: '1px solid #5a411f',
    color: '#e6c07a',
    borderRadius: '4px',
    boxShadow: '0 12px 30px rgba(0,0,0,0.35)',
  },
  '.cm-tooltip.cm-tooltip-autocomplete > ul > li[aria-selected]': {
    background: '#d3b49890',
    color: '#18110a',
  },
  '.cm-completionDetail': {
    color: '#a08555',
  },
  '.cm-berry-doc': {
    maxWidth: '360px',
    padding: '8px 10px',
    fontFamily: 'var(--dc-font-ui)',
    fontSize: '12px',
    lineHeight: '1.45',
  },
  '.cm-berry-doc code': {
    display: 'block',
    marginBottom: '6px',
    color: '#ffa24a',
    fontFamily: 'var(--dc-font-mono)',
    whiteSpace: 'pre-wrap',
  },
  '.cm-berry-doc p': {
    margin: '0',
    color: '#e6c07a',
  },

  /* Search / replace panel — flat selectors only (no nested &). */
  '.cm-panel.cm-search': {
    backgroundColor: '#2a1f10',
    color: '#e6c07a',
    borderBottom: '1px solid #332514',
    padding: '6px 8px',
    fontFamily: 'var(--dc-font-ui)',
    fontSize: '12px',
  },
  '.cm-panel.cm-search input, .cm-panel.cm-search button, .cm-panel.cm-search label': {
    margin: '.15em .4em .15em 0',
    verticalAlign: 'middle',
  },
  '.cm-panel.cm-search input': {
    backgroundColor: '#18110a',
    color: '#f4d9a1',
    border: '1px solid #5a411f',
    borderRadius: '3px',
    padding: '2px 6px',
    fontFamily: 'var(--dc-font-mono)',
    fontSize: '12px',
  },
  '.cm-panel.cm-search input:focus': {
    outline: 'none',
    borderColor: '#e07b1f',
  },
  '.cm-panel.cm-search button': {
    background: '#2a1f10',
    color: '#e6c07a',
    border: '1px solid #5a411f',
    borderRadius: '3px',
    padding: '2px 8px',
    cursor: 'pointer',
    fontSize: '11px',
    fontFamily: 'var(--dc-font-ui)',
    display: 'inline-flex',
    alignItems: 'center',
    appearance: 'none',
  },
  '.cm-panel.cm-search button:hover': {
    backgroundColor: '#362712',
    borderColor: '#a08555',
  },
  '.cm-panel.cm-search label': {
    color: '#a08555',
    fontSize: '11px',
    whiteSpace: 'pre',
    display: 'inline-flex',
    alignItems: 'center',
    lineHeight: '1',
  },
  '.cm-panel.cm-search [name=close]': {
    position: 'absolute',
    top: '4px',
    right: '6px',
    border: 'none',
    fontSize: '14px',
    padding: '0 4px',
    lineHeight: '1',
    color: '#a08555',
  },
  '.cm-panel.cm-search [name=close]:hover': {
    color: '#e6c07a',
    backgroundColor: 'transparent',
  },
  '.cm-dark .cm-searchMatch': {
    backgroundColor: '#e07c1f40',
    outline: '1px solid #e07c1f80',
  },
  '.cm-dark .cm-searchMatch-selected': {
    backgroundColor: '#e07b1f80',
    outline: '1px solid #e07b1f',
  },
}, { dark: true });

export function createCodeMirrorExtensions(onSave?: () => void, language = 'berry'): Extension[] {
  const languageExtensions = language === 'berry'
    ? [
        berryLanguage,
        syntaxHighlighting(berryHighlight),
        autocompletion({ override: [completeBerry], activateOnTyping: true }),
        berryHover,
      ]
    : [syntaxHighlighting(defaultHighlightStyle, { fallback: true })];

  return [
    warmTheme,
    EditorState.tabSize.of(2),
    indentUnit.of('  '),
    lineNumbers(),
    highlightActiveLineGutter(),
    drawSelection(),
    dropCursor(),
    bracketMatching(),
    closeBrackets(),
    indentOnInput(),
    highlightActiveLine(),
    highlightSelectionMatches(),
    ...languageExtensions,
    keymap.of([
      {
        key: 'Mod-s',
        preventDefault: true,
        run: () => {
          onSave?.();
          return true;
        },
      },
      ...searchKeymap,
    ]),
  ];
}
