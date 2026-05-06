/*
 * Monaco editor bootstrap.
 *
 * Vite needs us to wire up the editor's web workers manually. We only use
 * the plain editor (no language servers), so editor.worker is the only
 * worker required.
 *
 * Importing this module also registers the custom 'berry' language and a
 * static completion provider for the device's scripting API. Idempotent —
 * calling registerBerry() twice is a no-op.
 */
/* Import the editor API directly (not the umbrella `monaco-editor` entry,
 * which pulls every built-in language and balloons the bundle by ~3 MB).
 * The subpath has no type declarations of its own, so we type-import from
 * the umbrella entry — types are erased at compile time so this doesn't
 * pull any of those modules into the runtime bundle. */
// @ts-expect-error subpath has no .d.ts; types come from umbrella import below
import * as monacoRuntime from 'monaco-editor/esm/vs/editor/editor.api';
import type * as MonacoNS from 'monaco-editor';
import editorWorker from 'monaco-editor/esm/vs/editor/editor.worker?worker';

const monaco = monacoRuntime as typeof MonacoNS;

// eslint-disable-next-line @typescript-eslint/no-explicit-any
(self as any).MonacoEnvironment = {
  getWorker: () => new editorWorker(),
};

let registered = false;

interface BindingDoc {
  label: string;             // text inserted (the function name)
  detail?: string;           // signature shown in the menu
  documentation?: string;    // hover/documentation
  /** Snippet body inserted on Tab — uses Monaco placeholder syntax `${1:name}`.
   *  When omitted, only the bare label is inserted. */
  snippet?: string;
}

/* The scripting API as the firmware exposes it (berry_bindings.c). The UI
 * shouldn't drift from the device — keep this list in sync with be_regfunc()
 * calls there.
 *
 * Naming hierarchy: can_<level>_<verb>
 *   raw frame:  can_send_raw, can_recv_raw   (no DBC, no encoding)
 *   message:    can_msg_get, can_msg_set, can_msg_send   (decoded, by id)
 *   signal:     can_signal_get, can_signal_on   (scoped by message name) */
const API: BindingDoc[] = [
  { label: 'can_send_raw',
    detail: 'can_send_raw(bus:int, id:int, data:bytes)',
    documentation: 'Transmit a raw frame. No DBC encoding, no rate limiting.',
    snippet: 'can_send_raw(${1:bus}, ${2:id}, ${3:data})' },
  { label: 'can_recv_raw',
    detail: 'can_recv_raw(bus:int) -> [id, bytes] | nil',
    documentation: 'Pop one received frame from the bus, or nil if empty.',
    snippet: 'can_recv_raw(${1:bus})' },
  { label: 'can_signal_get',
    detail: 'can_signal_get(msg:str, sig:str [, bus:int]) -> {value, prev, changed} | nil',
    documentation: 'Read the current decoded value of a DBC signal. Scoped by message — DBC signal names are unique only within a message.',
    snippet: 'can_signal_get("${1:msg}", "${2:sig}")' },
  { label: 'can_signal_on',
    detail: 'can_signal_on(msg:str, sig:str, fn(sig) [, bus:int])',
    documentation: 'Invoke fn(sig) whenever the signal changes. sig is {value, prev, sig_idx}. Scoped by message — DBC signal names collide across messages.',
    snippet: 'can_signal_on("${1:msg}", "${2:sig}", def(s)\n  ${3:# body}\nend)' },
  { label: 'can_msg_get',
    detail: 'can_msg_get(id:int [, bus:int]) -> draft',
    documentation: 'Take a draft of the latest rx for this message id. Edit signals with can_msg_set, then can_msg_send to transmit.',
    snippet: 'can_msg_get(${1:0x123})' },
  { label: 'can_msg_set',
    detail: 'can_msg_set(draft, sig:str, value)',
    documentation: 'Modify one signal in the draft, leaving other bits intact. Signal lookup is scoped to the draft\'s message.',
    snippet: 'can_msg_set(${1:msg}, "${2:sig}", ${3:value})' },
  { label: 'can_msg_send',
    detail: 'can_msg_send(draft)',
    documentation: 'Transmit the draft. Auto-handles checksum and counter signals.',
    snippet: 'can_msg_send(${1:msg})' },
  { label: 'led_set',
    detail: 'led_set(r:int, g:int, b:int)',
    documentation: 'Set the on-board RGB LED colour (0–255 per channel).',
    snippet: 'led_set(${1:r}, ${2:g}, ${3:b})' },
  { label: 'led_off',
    detail: 'led_off()',
    documentation: 'Turn the on-board RGB LED off.',
    snippet: 'led_off()' },
  { label: 'state_set',
    detail: 'state_set(key:str, value:str)',
    documentation: 'Persist a key/value pair in this script\'s NVS namespace.',
    snippet: 'state_set("${1:key}", ${2:value})' },
  { label: 'state_get',
    detail: 'state_get(key:str [, default]) -> str | default',
    documentation: 'Read a previously stored value.',
    snippet: 'state_get("${1:key}")' },
  { label: 'state_remove',
    detail: 'state_remove(key:str)',
    documentation: 'Delete a stored key.',
    snippet: 'state_remove("${1:key}")' },
  { label: 'timer_after',
    detail: 'timer_after(ms:int, fn) -> handle',
    documentation: 'Run fn once, ms milliseconds from now.',
    snippet: 'timer_after(${1:ms}, ${2:fn})' },
  { label: 'timer_every',
    detail: 'timer_every(ms:int, fn) -> handle',
    documentation: 'Run fn every ms milliseconds. Returns a cancellable handle.',
    snippet: 'timer_every(${1:ms}, ${2:fn})' },
  { label: 'timer_cancel',
    detail: 'timer_cancel(handle)',
    documentation: 'Cancel a previously scheduled timer.',
    snippet: 'timer_cancel(${1:handle})' },
  { label: 'action_register',
    detail: 'action_register(name:str, fn)',
    documentation: 'Register fn as an Action. Becomes a tile on the Dashboard.',
    snippet: 'action_register("${1:name}", ${2:fn})' },
  { label: 'action_invoke',
    detail: 'action_invoke(name:str)',
    documentation: 'Invoke a registered action by name.',
    snippet: 'action_invoke("${1:name}")' },
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
    snippet: 'print(${1:msg})' },
];

const KEYWORDS = [
  'def', 'end', 'if', 'elif', 'else', 'while', 'for', 'in', 'do',
  'break', 'continue', 'return', 'var', 'true', 'false', 'nil',
  'and', 'or', 'class', 'static', 'self', 'super', 'import', 'as',
  'try', 'except', 'raise', 'new',
];

export function registerBerry(): void {
  if (registered) return;
  registered = true;

  monaco.languages.register({ id: 'berry', extensions: ['.be'] });

  monaco.languages.setLanguageConfiguration('berry', {
    comments: { lineComment: '#' },
    brackets: [['(', ')'], ['[', ']'], ['{', '}']],
    autoClosingPairs: [
      { open: '(', close: ')' },
      { open: '[', close: ']' },
      { open: '{', close: '}' },
      { open: '"', close: '"' },
      { open: "'", close: "'" },
    ],
  });

  monaco.languages.setMonarchTokensProvider('berry', {
    defaultToken: '',
    keywords: KEYWORDS,
    builtins: API.map(b => b.label),
    tokenizer: {
      root: [
        [/#.*$/, 'comment'],
        [/"([^"\\]|\\.)*"/, 'string'],
        [/'([^'\\]|\\.)*'/, 'string'],
        [/0x[0-9A-Fa-f]+/, 'number.hex'],
        [/\d+(\.\d+)?/, 'number'],
        [/[a-zA-Z_][\w]*/, {
          cases: {
            '@keywords': 'keyword',
            '@builtins': 'type.identifier',
            '@default':  'identifier',
          }
        }],
        [/[{}()[\]]/, '@brackets'],
        [/[<>!=+\-*/%&|^~]+/, 'operator'],
      ],
    },
  });

  /* Completion provider. Monaco binds Ctrl+Space to "trigger suggestion" by
   * default; quickSuggestions on the editor (see MonacoEditor.svelte) makes
   * them also pop up while typing. triggerCharacters covers the case where
   * the cursor isn't inside a word yet (e.g. right after a paren). */
  monaco.languages.registerCompletionItemProvider('berry', {
    triggerCharacters: ['_', '.', '(', ',', ' '],
    provideCompletionItems(model, position) {
      const word = model.getWordUntilPosition(position);
      const range = {
        startLineNumber: position.lineNumber,
        endLineNumber:   position.lineNumber,
        startColumn:     word.startColumn,
        endColumn:       word.endColumn,
      };
      const suggestions: MonacoNS.languages.CompletionItem[] = API.map(b => ({
        label: b.label,
        kind:  monaco.languages.CompletionItemKind.Function,
        insertText: b.snippet ?? b.label,
        insertTextRules: b.snippet
          ? monaco.languages.CompletionItemInsertTextRule.InsertAsSnippet
          : undefined,
        detail:        b.detail,
        documentation: b.documentation,
        range,
      }));
      for (const k of KEYWORDS) {
        suggestions.push({
          label: k,
          kind:  monaco.languages.CompletionItemKind.Keyword,
          insertText: k,
          range,
        });
      }
      return { suggestions };
    },
  });

  monaco.languages.registerHoverProvider('berry', {
    provideHover(model, position) {
      const word = model.getWordAtPosition(position);
      if (!word) return null;
      const b = API.find(x => x.label === word.word);
      if (!b) return null;
      return {
        contents: [
          { value: '```berry\n' + (b.detail ?? b.label) + '\n```' },
          { value: b.documentation ?? '' },
        ],
      };
    },
  });

  /* Kimbie warm theme — mirrors the --dc-* tokens in app.css so the editor
   * doesn't fight the surrounding chrome. Update both files in lockstep. */
  monaco.editor.defineTheme('dorky-dark', {
    base: 'vs-dark',
    inherit: true,
    rules: [
      { token: '',                foreground: 'f4d9a1' },
      { token: 'comment',         foreground: '70583a', fontStyle: 'italic' },
      { token: 'keyword',         foreground: 'c97a4a' },
      { token: 'type.identifier', foreground: 'b8c47a' },
      { token: 'string',          foreground: 'ffa24a' },
      { token: 'number',          foreground: 'd68a3a' },
      { token: 'number.hex',      foreground: 'd68a3a' },
      { token: 'identifier',      foreground: 'f4d9a1' },
      { token: 'operator',        foreground: 'e6c07a' },
      { token: 'delimiter',       foreground: 'a08555' },
    ],
    colors: {
      'editor.background':                '#18110a',
      'editor.foreground':                '#f4d9a1',
      'editorCursor.foreground':          '#e07b1f',
      'editor.lineHighlightBackground':   '#2a1f10',
      'editor.lineHighlightBorder':       '#00000000',
      'editorLineNumber.foreground':      '#70583a',
      'editorLineNumber.activeForeground':'#e6c07a',
      'editor.selectionBackground':       '#e07b1f52',
      'editor.inactiveSelectionBackground':'#e07b1f29',
      'editorWidget.background':          '#362712',
      'editorWidget.border':              '#5a411f',
      'editorSuggestWidget.background':   '#362712',
      'editorSuggestWidget.border':       '#5a411f',
      'editorSuggestWidget.selectedBackground': '#4a351b',
      'editorSuggestWidget.foreground':   '#e6c07a',
      'editorIndentGuide.background1':    '#3d2d16',
      'editorIndentGuide.activeBackground1': '#5a411f',
      'editorBracketMatch.background':    '#4a2c1066',
      'editorBracketMatch.border':        '#d68a3a',
      'scrollbarSlider.background':       '#4a351b66',
      'scrollbarSlider.hoverBackground':  '#6b4a24aa',
      'scrollbarSlider.activeBackground': '#6b4a24',
    },
  });
}

export { monaco };
