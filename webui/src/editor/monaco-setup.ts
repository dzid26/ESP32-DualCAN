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
 * which pulls every built-in language and balloons the bundle by ~3 MB). */
import * as monaco from 'monaco-editor/esm/vs/editor/editor.api';
import editorWorker from 'monaco-editor/esm/vs/editor/editor.worker?worker';

// eslint-disable-next-line @typescript-eslint/no-explicit-any
(self as any).MonacoEnvironment = {
  getWorker: () => new editorWorker(),
};

let registered = false;

interface BindingDoc {
  label: string;             // text inserted
  detail?: string;           // signature shown in the menu
  documentation?: string;    // hover/documentation
}

/* The scripting API as the firmware exposes it (berry_bindings.c). The UI
 * shouldn't drift from the device — keep this list in sync with be_regfunc()
 * calls there. */
const API: BindingDoc[] = [
  { label: 'can_send',       detail: 'can_send(bus:int, id:int, data:bytes)',
    documentation: 'Transmit a raw frame on the given bus.' },
  { label: 'can_receive',    detail: 'can_receive(bus:int) -> [id, bytes] | nil',
    documentation: 'Pop one received frame from the bus, or nil if empty.' },
  { label: 'can_signal',     detail: "can_signal(name:str [, bus:int]) -> {value, prev, changed} | nil",
    documentation: 'Read current decoded value of a DBC signal.' },
  { label: 'can_on',         detail: 'can_on(name:str, fn(sig), [, bus:int])',
    documentation: 'Invoke fn(sig) every time the named signal changes. sig is {value, prev, sig_idx}.' },
  { label: 'can_msg_draft',  detail: 'can_msg_draft(id:int [, bus:int]) -> draft',
    documentation: 'Take a draft of the latest rx for this message id.' },
  { label: 'can_msg_set',    detail: 'can_msg_set(draft, signal_name:str, value)',
    documentation: 'Modify one signal in the draft, leaving other bits intact.' },
  { label: 'can_msg_send',   detail: 'can_msg_send(draft)',
    documentation: 'Transmit the draft. Auto-handles checksum and counter signals.' },
  { label: 'led_set',        detail: 'led_set(r:int, g:int, b:int)',
    documentation: 'Set the on-board RGB LED colour (0–255 per channel).' },
  { label: 'led_off',        detail: 'led_off()',
    documentation: 'Turn the on-board RGB LED off.' },
  { label: 'state_set',      detail: 'state_set(key:str, value:str)',
    documentation: 'Persist a key/value pair in this script\'s NVS namespace.' },
  { label: 'state_get',      detail: 'state_get(key:str [, default]) -> str | default',
    documentation: 'Read a previously stored value.' },
  { label: 'state_remove',   detail: 'state_remove(key:str)',
    documentation: 'Delete a stored key.' },
  { label: 'timer_after',    detail: 'timer_after(ms:int, fn) -> handle',
    documentation: 'Run fn once, ms milliseconds from now.' },
  { label: 'timer_every',    detail: 'timer_every(ms:int, fn) -> handle',
    documentation: 'Run fn every ms milliseconds. Returns a cancellable handle.' },
  { label: 'timer_cancel',   detail: 'timer_cancel(handle)',
    documentation: 'Cancel a previously scheduled timer.' },
  { label: 'action_register', detail: 'action_register(name:str, fn)',
    documentation: 'Register fn as an Action. Becomes a tile on the Dashboard.' },
  { label: 'action_invoke',  detail: 'action_invoke(name:str)',
    documentation: 'Invoke a registered action by name.' },
  { label: 'action_list',    detail: 'action_list() -> [name…]',
    documentation: 'Return the names of all currently-registered actions.' },
  { label: 'millis',         detail: 'millis() -> int',
    documentation: 'Milliseconds since boot. Wraps at 2^31.' },
  { label: 'print',          detail: 'print(...)',
    documentation: 'Write a line to the device log; streams to the Dashboard log panel.' },
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

  monaco.languages.registerCompletionItemProvider('berry', {
    provideCompletionItems(model, position) {
      const word = model.getWordUntilPosition(position);
      const range = {
        startLineNumber: position.lineNumber,
        endLineNumber:   position.lineNumber,
        startColumn:     word.startColumn,
        endColumn:       word.endColumn,
      };
      const suggestions: monaco.languages.CompletionItem[] = API.map(b => ({
        label: b.label,
        kind:  monaco.languages.CompletionItemKind.Function,
        insertText:    b.label,
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

  monaco.editor.defineTheme('dorky-dark', {
    base: 'vs-dark',
    inherit: true,
    rules: [
      { token: 'comment',         foreground: '6a6a8a', fontStyle: 'italic' },
      { token: 'keyword',         foreground: 'c792ea' },
      { token: 'type.identifier', foreground: '82aaff' },
      { token: 'string',          foreground: 'c3e88d' },
      { token: 'number',          foreground: 'f78c6c' },
      { token: 'number.hex',      foreground: 'f78c6c' },
    ],
    colors: {
      'editor.background': '#0d0d1a',
    },
  });
}

export { monaco };
