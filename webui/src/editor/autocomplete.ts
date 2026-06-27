import { snippetCompletion, type Completion, type CompletionContext, type CompletionResult } from '@codemirror/autocomplete';
import { hoverTooltip, type Tooltip } from '@codemirror/view';
import { dbcStore } from '../dbcStore.svelte';

interface DbcCallInfo {
  fnName: string;
  argIndex: number;
  argValues: string[];
  bus: number;
}

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
 *   message:    can_msg_get, msg_sig_set, can_msg_send   (decoded, by id)
 *   signal:     msg_sig_get, on_can_signal   (scoped by message name) */
const API: BindingDoc[] = [
  { label: 'can_send_raw',
    detail: 'can_send_raw(bus:int, id:int, data:bytes)',
    documentation: 'Transmit a raw frame. No DBC encoding, no rate limiting.',
    snippet: 'can_send_raw(${bus}, ${id}, ${data})' },
  { label: 'can_recv_raw',
    detail: 'can_recv_raw(bus:int, msg_id:int [, timeout_ms:int]) -> bytes | nil',
    documentation: 'Returns last payload for a CAN ID. Default 1000 ms timeout — first read blocks up to 1 s for initial data. Pass 0 for instant return. Blocking stalls Berry VM, only use in setup().',
    snippet: 'can_recv_raw(${bus}, ${msg_id})' },
  { label: 'msg_sig_get',
    detail: 'msg_sig_get(draft, sig:str) -> int',
    documentation: 'Extract a raw integer bitfield from a pre-read message draft. Nil-guard the draft before calling.',
    snippet: 'msg_sig_get(${msg}, "${sig}")' },
  { label: 'on_can_signal',
    detail: 'on_can_signal(msg:str, sig:str, fn(sig) [, bus:int])',
    documentation: 'Invoke fn(sig) whenever the signal changes. sig is {value, prev, sig_idx}. Scoped by message — DBC signal names collide across messages.',
    snippet: 'on_can_signal("${msg}", "${sig}", def(s)\n  ${body}\nend)' },
  { label: 'can_msg_new',
    detail: 'can_msg_new(name:str) | can_msg_new(id:int, dlc:int) -> draft',
    documentation: 'Create a fresh zeroed message draft. Name form auto-fills DLC from DBC. Use with msg_sig_set, then can_msg_send.',
    snippet: 'can_msg_new("${msg}")' },
  { label: 'can_msg_get',
    detail: 'can_msg_get(bus:int, id:int | name:str) -> draft | nil',
    documentation: 'Take a draft of the latest rx for this message id or DBC message name. bus is always first. Returns nil if no frame received — use can_msg_new for a fresh draft.',
    snippet: 'can_msg_get(${bus}, "${msg}")' },
  { label: 'msg_sig_set',
    detail: 'msg_sig_set(draft, sig:str, value)',
    documentation: 'Modify one signal in the draft, leaving other bits intact. Signal lookup is scoped to the draft\'s message.',
    snippet: 'msg_sig_set(${msg}, "${sig}", ${value})' },
  { label: 'can_msg_send',
    detail: 'can_msg_send(bus:int, draft) -> nil',
    documentation: 'Transmit a message draft on the given bus. bus is first for consistency with can_send_raw, can_recv_raw.',
    snippet: 'can_msg_send(${bus}, ${msg})' },
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

const BERRY_BUILTINS = [
  'assert', 'bool', 'input', 'classname', 'classof', 'number', 'real',
  'bytes', 'compile', 'map', 'list', 'int', 'isinstance', 'range',
  'str', 'module', 'size', 'issubclass', 'open', 'file', 'type', 'call',
];

export const BUILTINS = new Set([...API.map(b => b.label), ...BERRY_BUILTINS]);
export const KEYWORD_SET = new Set(KEYWORDS);

/* Functions that expect a DBC message name as the first string argument. */
const DBC_MSG_FUNCS = new Set(['on_can_signal', 'can_msg_get', 'can_msg_new']);
/* Functions that also expect a DBC signal name as the second string argument. */
const DBC_SIG_FUNCS = new Set(['msg_sig_get', 'on_can_signal']);

const berryCompletions: Completion[] = [
  ...API.map(binding => snippetCompletion(binding.snippet ?? binding.label, {
    label: binding.label,
    type: 'function',
    detail: binding.detail,
    info: binding.documentation,
    boost: 10,
  })),
  ...KEYWORDS.map(label => ({ label, type: 'keyword' as const })),
  ...BERRY_BUILTINS.map(label => ({
    label,
    type: 'function' as const,
    detail: 'Berry builtin',
    boost: 5,
  })),
];

const MAX_DBC = 200;

/* Return DBC message names filtered by prefix, capped. */
function dbcMsgCompletions(prefix: string, insideString: boolean, busHint: string): Completion[] {
  const lower = prefix.toLowerCase();
  return dbcStore.messages
    .filter(m => !lower || m.name.toLowerCase().includes(lower))
    .slice(0, MAX_DBC)
    .map(msg => ({
      label: insideString ? msg.name : `"${msg.name}"`,
      filterText: msg.name,
      type: 'type' as const,
      detail: `0x${msg.id.toString(16)}` + (busHint ? ` (${busHint})` : ''),
      apply: insideString ? msg.name : `"${msg.name}"`,
    }));
}

/* Return DBC signal names filtered by prefix and optional message scope, capped. */
function dbcSigCompletions(prefix: string, insideString: boolean, busHint: string, msgName?: string): Completion[] {
  const lower = prefix.toLowerCase();
  const candidates = msgName
    ? dbcStore.signals.filter(s => s.message === msgName)
    : dbcStore.signals;
  return candidates
    .filter(s => !lower || s.name.toLowerCase().includes(lower))
    .slice(0, MAX_DBC)
    .map(sig => ({
      label: insideString ? sig.name : `"${sig.name}"`,
      filterText: sig.name,
      type: 'property' as const,
      detail: sig.message + (busHint ? ` (${busHint})` : ''),
      apply: insideString ? sig.name : `"${sig.name}"`,
    }));
}

function dbcCompletions(textBefore: string, wordFrom: number, insideString: boolean): CompletionResult | null {
  const callInfo = findDbcCall(textBefore);
  if (!callInfo) return null;
  const busHint = callInfo.bus >= 0 ? `bus ${callInfo.bus}` : '';
  const prefix = textBefore.slice(wordFrom);

  if (callInfo.argIndex === 0) {
    const options = dbcMsgCompletions(prefix, insideString, busHint);
    if (options.length === 0) return null;
    return { from: wordFrom, options, validFor: /^\w*$/ };
  }

  if (callInfo.argIndex === 1) {
    const msgName = callInfo.argValues[0];
    const options = dbcSigCompletions(prefix, insideString, busHint, msgName);
    if (options.length === 0) return null;
    return { from: wordFrom, options, validFor: /^\w*$/ };
  }

  return null;
}

/* True if the non-whitespace content before cursor on this line is empty,
   suggesting we're at a statement-start position. */
function atStatementStart(textBefore: string): boolean {
  const line = textBefore.split('\n').pop() ?? '';
  return line.trim() === '';
}

/* Determine cursor context by scanning the text left of the cursor.
   Berry tokeniser processes # (comment) before strings, so a # anywhere
   before a quote takes precedence. */
type CursorContext = 'comment' | 'string' | 'code';

function findContext(text: string): CursorContext {
  let inString = false;
  let quoteChar = '';
  for (let i = 0; i < text.length; i++) {
    const ch = text[i];
    if (ch === '\\') { i++; continue; }
    if (!inString && ch === '#') return 'comment';
    if (ch === '"' || ch === "'") {
      if (!inString) { inString = true; quoteChar = ch; }
      else if (ch === quoteChar) { inString = false; }
    }
  }
  return inString ? 'string' : 'code';
}

/* Scan text before the cursor to detect whether we're inside a DBC-aware
   function call, which argument index we're typing, and the values of any
   preceding string arguments. */
function findDbcCall(text: string): DbcCallInfo | null {
  let depth = 0;
  let parenPos = -1;
  for (let i = text.length - 1; i >= 0; i--) {
    const ch = text[i];
    if (ch === ')') depth++;
    else if (ch === '(') {
      depth--;
      if (depth < 0) { parenPos = i; break; }
    }
  }
  if (parenPos < 0) return null;

  const beforeParen = text.slice(0, parenPos).trimEnd();
  const fnMatch = beforeParen.match(/([A-Za-z_]\w*)$/);
  if (!fnMatch) return null;
  const fnName = fnMatch[1];
  if (!DBC_MSG_FUNCS.has(fnName)) return null;

  const argText = text.slice(parenPos + 1);
  const argValues: string[] = [];
  let commaCount = 0;
  let i = 0;
  while (i < argText.length) {
    const ch = argText[i];
    if (ch === '"' || ch === "'") {
      const quote = ch;
      const start = i + 1;
      let j = start;
      while (j < argText.length && argText[j] !== quote) {
        if (argText[j] === '\\') j++;
        j++;
      }
      const terminated = j < argText.length;
      argValues.push(argText.slice(start, j));
      if (!terminated) break;
      i = j + 1;
    } else if (ch === ',') {
      commaCount++;
      i++;
    } else {
      i++;
    }
  }
  const argIndex = commaCount;

  return { fnName, argIndex, argValues, bus: 0 };
}

export function completeBerry(context: CompletionContext): CompletionResult | null {
  const word = context.matchBefore(/\w*/);
  if (!word) return null;
  const wordEmpty = word.from === word.to;

  const textBefore = context.state.sliceDoc(0, context.pos);

  const ctx = findContext(textBefore);

  if (ctx === 'comment') {
    if (wordEmpty && !context.explicit) return null;
    const busHint = `bus 0`;
    const prefix = textBefore.slice(word.from);
    const opts: Completion[] = [
      ...dbcSigCompletions(prefix, true, busHint),
      ...dbcMsgCompletions(prefix, true, busHint),
    ];
    if (opts.length === 0) return null;
    return { from: word.from, options: opts, validFor: /^\w*$/ };
  }

  if (ctx === 'string') {
    const result = dbcCompletions(textBefore, word.from, true);
    if (result) return result;
    if (wordEmpty) return null;
  }

  if (wordEmpty && !context.explicit) return null;

  const callInfo = findDbcCall(textBefore);

  if (callInfo) {
    const result = dbcCompletions(textBefore, word.from, false);
    if (result) return result;
  }

  const atStart = atStatementStart(textBefore);

  const opts: Completion[] = [];

  if (atStart) {
    opts.push(...berryCompletions, ...KEYWORDS.map(label => ({ label, type: 'keyword' as const })));
    const busHint = `bus 0`;
    const prefix = textBefore.slice(word.from);
    opts.push(...dbcSigCompletions(prefix, true, busHint).map(o => ({ ...o, boost: 5 })));
    opts.push(...dbcMsgCompletions(prefix, true, busHint).map(o => ({ ...o, boost: 5 })));
  } else if (callInfo) {
    opts.push(...KEYWORDS.map(label => ({ label, type: 'keyword' as const })));
  }

  if (opts.length === 0) return null;
  return { from: word.from, options: opts, validFor: /^\w*$/ };
}

function wordBounds(text: string, pos: number): { from: number; to: number; word: string } | null {
  let from = pos;
  let to = pos;
  while (from > 0 && /[\w]/.test(text[from - 1])) from--;
  while (to < text.length && /[\w]/.test(text[to])) to++;
  if (from === to) return null;
  return { from, to, word: text.slice(from, to) };
}

export const berryHover = hoverTooltip((view, pos): Tooltip | null => {
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
