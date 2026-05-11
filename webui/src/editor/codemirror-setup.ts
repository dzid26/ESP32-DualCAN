/*
 * CodeMirror 6 editor setup.
 *
 * The web UI uses CodeMirror's core packages directly rather than an editor
 * wrapper. This module owns the custom Berry stream tokenizer, static device
 * API completions, hover docs, and the warm theme shared with app.css.
 */
import { autocompletion, closeBrackets, snippetCompletion, type Completion, type CompletionContext, type CompletionResult } from '@codemirror/autocomplete';
import { bracketMatching, defaultHighlightStyle, indentOnInput, indentUnit, syntaxHighlighting, HighlightStyle, StreamLanguage, type StreamParser } from '@codemirror/language';
import { highlightSelectionMatches, searchKeymap } from '@codemirror/search';
import { EditorState, type Extension } from '@codemirror/state';
import { drawSelection, dropCursor, EditorView, highlightActiveLine, highlightActiveLineGutter, hoverTooltip, keymap, lineNumbers, type Tooltip } from '@codemirror/view';
import { tags } from '@lezer/highlight';

interface BindingDoc {
  label: string;
  detail?: string;
  documentation?: string;
  /** Snippet body inserted by CodeMirror; placeholders use `${name}` syntax. */
  snippet?: string;
}

/* The scripting API as the firmware exposes it (berry_bindings.c). The UI
 * shouldn't drift from the device — keep this list in sync with be_regfunc()
 * calls there.
 *
 * Naming hierarchy: can_<level>_<verb>
 *   raw frame:  can_send_raw, can_recv_raw   (no DBC, no encoding)
 *   message:    can_msg_get, can_msg_set, can_msg_send   (decoded, by id)
 *   signal:     can_signal_get, on_can_signal   (scoped by message name) */
const API: BindingDoc[] = [
  { label: 'can_send_raw',
    detail: 'can_send_raw(bus:int, id:int, data:bytes)',
    documentation: 'Transmit a raw frame. No DBC encoding, no rate limiting.',
    snippet: 'can_send_raw(${bus}, ${id}, ${data})' },
  { label: 'can_recv_raw',
    detail: 'can_recv_raw(bus:int) -> [id, bytes] | nil',
    documentation: 'Pop one received frame from the bus, or nil if empty.',
    snippet: 'can_recv_raw(${bus})' },
  { label: 'can_signal_get',
    detail: 'can_signal_get(msg:str, sig:str [, bus:int]) -> {value, prev, changed} | nil',
    documentation: 'Read the current decoded value of a DBC signal. Scoped by message — DBC signal names are unique only within a message.',
    snippet: 'can_signal_get("${msg}", "${sig}")' },
  { label: 'on_can_signal',
    detail: 'on_can_signal(msg:str, sig:str, fn(sig) [, bus:int])',
    documentation: 'Invoke fn(sig) whenever the signal changes. sig is {value, prev, sig_idx}. Scoped by message — DBC signal names collide across messages.',
    snippet: 'on_can_signal("${msg}", "${sig}", def(s)\n  ${body}\nend)' },
  { label: 'can_msg_get',
    detail: 'can_msg_get(id:int [, bus:int]) -> draft',
    documentation: 'Take a draft of the latest rx for this message id. Edit signals with can_msg_set, then can_msg_send to transmit.',
    snippet: 'can_msg_get(${id})' },
  { label: 'can_msg_set',
    detail: 'can_msg_set(draft, sig:str, value)',
    documentation: 'Modify one signal in the draft, leaving other bits intact. Signal lookup is scoped to the draft\'s message.',
    snippet: 'can_msg_set(${msg}, "${sig}", ${value})' },
  { label: 'can_msg_send',
    detail: 'can_msg_send(draft)',
    documentation: 'Transmit the draft. Auto-handles checksum and counter signals.',
    snippet: 'can_msg_send(${msg})' },
  { label: 'led_set',
    detail: 'led_set(r:int, g:int, b:int)',
    documentation: 'Set the on-board RGB LED colour (0–255 per channel).',
    snippet: 'led_set(${r}, ${g}, ${b})' },
  { label: 'led_off',
    detail: 'led_off()',
    documentation: 'Turn the on-board RGB LED off.',
    snippet: 'led_off()' },
  { label: 'state_set',
    detail: 'state_set(key:str, value:str)',
    documentation: 'Persist a key/value pair in this script\'s NVS namespace.',
    snippet: 'state_set("${key}", ${value})' },
  { label: 'state_get',
    detail: 'state_get(key:str [, default]) -> str | default',
    documentation: 'Read a previously stored value.',
    snippet: 'state_get("${key}")' },
  { label: 'state_remove',
    detail: 'state_remove(key:str)',
    documentation: 'Delete a stored key.',
    snippet: 'state_remove("${key}")' },
  { label: 'timer_after',
    detail: 'timer_after(ms:int, fn) -> handle',
    documentation: 'Run fn once, ms milliseconds from now.',
    snippet: 'timer_after(${ms}, ${fn})' },
  { label: 'timer_every',
    detail: 'timer_every(ms:int, fn) -> handle',
    documentation: 'Run fn every ms milliseconds. Returns a cancellable handle.',
    snippet: 'timer_every(${ms}, ${fn})' },
  { label: 'timer_cancel',
    detail: 'timer_cancel(handle)',
    documentation: 'Cancel a previously scheduled timer.',
    snippet: 'timer_cancel(${handle})' },
  { label: 'action_register',
    detail: 'action_register(name:str, fn)',
    documentation: 'Register fn as an Action. Becomes a tile on the Dashboard.',
    snippet: 'action_register("${name}", ${fn})' },
  { label: 'action_invoke',
    detail: 'action_invoke(name:str)',
    documentation: 'Invoke a registered action by name.',
    snippet: 'action_invoke("${name}")' },
  { label: 'action_list',
    detail: 'action_list() -> [name…]',
    documentation: 'Return the names of all currently-registered actions.',
    snippet: 'action_list()' },
  { label: 'millis',
    detail: 'millis() -> int',
    documentation: 'Milliseconds since boot. Wraps at 2^31.',
    snippet: 'millis()' },
  { label: 'print',
    detail: 'print(...)',
    documentation: 'Write a line to the device log; streams to the Dashboard log panel.',
    snippet: 'print(${msg})' },
];

const KEYWORDS = [
  'def', 'end', 'if', 'elif', 'else', 'while', 'for', 'in', 'do',
  'break', 'continue', 'return', 'var', 'true', 'false', 'nil',
  'and', 'or', 'class', 'static', 'self', 'super', 'import', 'as',
  'try', 'except', 'raise', 'new',
];

const BUILTINS = new Set(API.map(b => b.label));
const KEYWORD_SET = new Set(KEYWORDS);
const IDENT = /[A-Za-z_][\w]*/;

const berryParser: StreamParser<unknown> = {
  name: 'berry',
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

const berryCompletions: Completion[] = [
  ...API.map(binding => snippetCompletion(binding.snippet ?? binding.label, {
    label: binding.label,
    type: 'function',
    detail: binding.detail,
    info: binding.documentation,
    boost: 10,
  })),
  ...KEYWORDS.map(label => ({ label, type: 'keyword' as const })),
];

function completeBerry(context: CompletionContext): CompletionResult | null {
  const word = context.matchBefore(/\w*/);
  if (!word || (word.from === word.to && !context.explicit)) return null;
  return {
    from: word.from,
    options: berryCompletions,
    validFor: /^\w*$/,
  };
}

function wordBounds(text: string, pos: number): { from: number; to: number; word: string } | null {
  let from = pos;
  let to = pos;
  while (from > 0 && /[\w]/.test(text[from - 1])) from--;
  while (to < text.length && /[\w]/.test(text[to])) to++;
  if (from === to) return null;
  return { from, to, word: text.slice(from, to) };
}

const berryHover = hoverTooltip((view, pos): Tooltip | null => {
  const line = view.state.doc.lineAt(pos);
  const bounds = wordBounds(line.text, pos - line.from);
  if (!bounds) return null;

  const binding = API.find(entry => entry.label === bounds.word);
  if (!binding) return null;

  return {
    pos: line.from + bounds.from,
    end: line.from + bounds.to,
    above: true,
    create() {
      const dom = document.createElement('div');
      dom.className = 'cm-berry-doc';

      const detail = document.createElement('code');
      detail.textContent = binding.detail ?? binding.label;
      dom.append(detail);

      if (binding.documentation) {
        const docs = document.createElement('p');
        docs.textContent = binding.documentation;
        dom.append(docs);
      }

      return { dom };
    },
  };
});

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
  },
  '.cm-content': {
    caretColor: '#e07b1f',
    padding: '8px 0',
  },
  '.cm-line': {
    padding: '0 10px',
  },
  '.cm-cursor, .cm-dropCursor': {
    borderLeftColor: '#e07b1f',
  },
  '.cm-selectionBackground, &.cm-focused .cm-selectionBackground, ::selection': {
    backgroundColor: '#e07b1f52',
  },
  '.cm-activeLine': {
    backgroundColor: '#2a1f10',
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
  '.cm-tooltip-autocomplete ul li[aria-selected]': {
    backgroundColor: '#4a351b',
    color: '#f4d9a1',
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
}, { dark: true });

/**
 * @param onSave     Called when the user triggers a save (Mod-s).
 * @param language   Language mode: 'berry' (default) or plain text.
 * @param checkAuth  Optional guard invoked before every save attempt.
 *                   Return `true` to allow the save, `false` to block it.
 *                   Defaults to always-allow so existing callers are unaffected,
 *                   but callers that handle privileged operations (e.g. uploading
 *                   scripts to the device) MUST supply a real auth check here.
 */
export function createCodeMirrorExtensions(
  onSave?: () => void,
  language = 'berry',
  checkAuth: () => boolean = () => true,
): Extension[] {
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
          if (!checkAuth()) return true;
          onSave?.();
          return true;
        },
      },
      ...searchKeymap,
    ]),
  ];
}
