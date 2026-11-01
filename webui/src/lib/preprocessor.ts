/**
 * Berry script preprocessor: converts `.be` source to `.bep` runtime format.
 *
 * Design: Self-contained preprocessed scripts with NO runtime DBC dependency.
 * All signal metadata is inlined as Berry code by the preprocessor.
 *
 * Conversion rules:
 *   can_msg_get(bus, "msg")         → can_msg_get(bus, 0xID)
 *   can_msg_new("msg")              → can_msg_new(0xID, dlc)
 *   msg_sig_get(draft, "sig")       → msg_sig_get(draft, sb, len, be, signed, scale, offset)
 *   msg_sig_set(draft, "sig", val)  → msg_sig_set(draft, sb, len, be, signed, scale, offset, val)
 * Missing signals/messages → raise("key_error", "...")
 *
 * Call matching is parenthesis-balanced and recursive, so DBC-aware calls
 * compose: a msg_sig_get nested inside a msg_sig_set value (e.g. incrementing
 * a counter field) is rewritten as a whole instead of being truncated by the
 * outer match. Anything the rewriter cannot safely parse (unbalanced
 * parentheses) is reported in errors[] with a runtime raise rather than
 * emitted as broken code. Signal names that can only be resolved by picking
 * the first of several matches are reported in warnings[] (non-fatal).
 */

import type { Message, Signal } from '../dbc/parser';

/* ---- Scripting API version ---- */
// The firmware reports scripting_api_version in system.info; the web UI warns
// before uploading a script when versions don't match.
export const SCRIPTING_API_VERSION = 1;

/* Build signal-decode/encode arg list from a Signal. */
function sigMeta(s: Signal): string {
  return [
    s.startBit,
    s.bitLength,
    s.isBigEndian ? 'true' : 'false',
    s.isSigned ? 'true' : 'false',
    s.scale,
    s.offset,
  ].join(', ');
}

/* Check if a position in source falls on a Berry-commented line (starts with #). */
function isCommented(source: string, pos: number): boolean {
  const before = source.slice(0, pos);
  const lastNewline = before.lastIndexOf('\n');
  const line = lastNewline < 0 ? before : before.slice(lastNewline + 1);
  return /^\s*#/.test(line);
}

/* ---- Balanced-paren scanning ---- */

/**
 * Given the index of an opening `(` in `text`, return the index of the
 * matching `)`, or -1 if unbalanced. String literals are skipped so parens
 * inside them don't affect nesting.
 */
function findMatchingParen(text: string, open: number): number {
  let depth = 0;
  let inString: string | null = null;
  for (let i = open; i < text.length; i++) {
    const c = text[i];
    if (inString) {
      if (c === '\\') { i++; continue; }
      if (c === inString) inString = null;
      continue;
    }
    if (c === '"' || c === "'") { inString = c; continue; }
    if (c === '(') depth++;
    else if (c === ')') {
      depth--;
      if (depth === 0) return i;
    }
  }
  return -1;
}

/**
 * Split a call's argument text on top-level commas, respecting nested
 * (), [], {} and string literals. Returns the raw (untrimmed) argument
 * slices so callers can recurse on them.
 */
function splitTopLevelArgs(text: string): string[] {
  const args: string[] = [];
  let depth = 0;
  let inString: string | null = null;
  let start = 0;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (inString) {
      if (c === '\\') { i++; continue; }
      if (c === inString) inString = null;
      continue;
    }
    if (c === '"' || c === "'") { inString = c; continue; }
    if (c === '(' || c === '[' || c === '{') depth++;
    else if (c === ')' || c === ']' || c === '}') depth--;
    else if (c === ',' && depth === 0) { args.push(text.slice(start, i)); start = i + 1; }
  }
  args.push(text.slice(start));
  return args;
}

/* Return the contents of a plain string-literal expression, or null. */
function stringLiteralValue(expr: string): string | null {
  const t = expr.trim();
  for (const q of ['"', "'"]) {
    if (t.length >= 2 && t.startsWith(q) && t.endsWith(q)) {
      const inner = t.slice(1, -1);
      if (!inner.includes(q)) return inner;
    }
  }
  return null;
}

/* ---- Track variable → message name mappings ---- */
// Scans source for var X = can_msg_get(bus, "msg") / can_msg_new("msg")
// assignments. Returns a map of variable name → set of message types it
// was assigned from (may be >1, indicating a reused variable).
export function trackVarToMsgs(source: string): Map<string, Set<string>> {
  const varToMsgs = new Map<string, Set<string>>();
  const varGetRe = /(?:^|\s)(?:var\s+)?(\w+)\s*=\s*can_msg_get\s*\(\s*\d+\s*,\s*"([^"]+)"\s*\)/g;
  let m: RegExpExecArray | null;
  while ((m = varGetRe.exec(source)) !== null) {
    if (isCommented(source, m.index)) continue;
    const varName = m[1];
    const msgName = m[2];
    if (!varToMsgs.has(varName)) varToMsgs.set(varName, new Set());
    varToMsgs.get(varName)!.add(msgName);
  }
  const varNewRe = /(?:^|\s)(?:var\s+)?(\w+)\s*=\s*can_msg_new\s*\(\s*"([^"]+)"\s*(?:,\s*\d+\s*)?\)/g;
  while ((m = varNewRe.exec(source)) !== null) {
    if (isCommented(source, m.index)) continue;
    const varName = m[1];
    const msgName = m[2];
    if (!varToMsgs.has(varName)) varToMsgs.set(varName, new Set());
    varToMsgs.get(varName)!.add(msgName);
  }
  return varToMsgs;
}

/** Like trackVarToMsgs but only returns variables with exactly one unique message. */
export function buildVarToMsgMap(source: string): Map<string, string> {
  const map = new Map<string, string>();
  const multiple = new Set<string>();
  for (const [varName, msgs] of trackVarToMsgs(source)) {
    if (msgs.size === 1 && !multiple.has(varName)) {
      map.set(varName, msgs.values().next().value!);
    }
  }
  return map;
}

/* ---- Main entry point ---- */

export function preprocessScript(
  sourceCode: string,
  messages: Message[]
): { code: string; errors: string[]; warnings: string[] } {
  const errors: string[] = [];
  const warnings: string[] = [];

  function findSigInMsg(msgName: string, sigName: string): { msg: Message; sig: Signal; idx: number } | null {
    for (const msg of messages) {
      if (msg.name === msgName) {
        for (let i = 0; i < msg.signals.length; i++) {
          if (msg.signals[i].name === sigName) {
            return { msg, sig: msg.signals[i], idx: i };
          }
        }
        return null;
      }
    }
    return null;
  }

  function findMsgId(msgName: string): number | null {
    for (const msg of messages) {
      if (msg.name === msgName) return msg.id;
    }
    return null;
  }

  /* All message entries that contain a signal of this name. */
  function signalMatches(sigName: string): Array<{ msg: Message; sig: Signal }> {
    const out: Array<{ msg: Message; sig: Signal }> = [];
    for (const msg of messages) {
      for (const sig of msg.signals) {
        if (sig.name === sigName) out.push({ msg, sig });
      }
    }
    return out;
  }

  const varToMsgs = trackVarToMsgs(sourceCode);

  function resolveSignalInDraft(
    draftExpr: string,
    sigName: string,
    context: string
  ): { sig: Signal | null; error: string | null; warning: string | null } {
    const isSimpleVar = /^\w+$/.test(draftExpr);
    const msgNames = isSimpleVar ? varToMsgs.get(draftExpr) : undefined;
    const uniqueMsgName = msgNames && msgNames.size === 1 ? msgNames.values().next().value : null;

    if (uniqueMsgName) {
      // We know exactly which message this variable belongs to.
      const found = findSigInMsg(uniqueMsgName, sigName);
      if (found) return { sig: found.sig, error: null, warning: null };

      // Signal not in the tracked message — figure out where it does live.
      const owners = [...new Set(signalMatches(sigName).map(m => m.msg.name))]
        .filter(n => n !== uniqueMsgName);
      if (owners.length === 1) {
        return {
          sig: null,
          error: `${context}: signal '${sigName}' belongs to '${owners[0]}', not '${uniqueMsgName}'`,
          warning: null,
        };
      }
      if (owners.length > 1) {
        return {
          sig: null,
          error: `${context}: signal '${sigName}' is not in message '${uniqueMsgName}', but is present in multiple messages (${owners.join(', ')})`,
          warning: null,
        };
      }
      return {
        sig: null,
        error: `${context}: signal '${sigName}' not found in message '${uniqueMsgName}'`,
        warning: null,
      };
    }

    // Untracked or multi-assigned variable — fall back to global search.
    const matches = signalMatches(sigName);
    if (matches.length === 0) {
      return { sig: null, error: `${context}: signal '${sigName}' not found`, warning: null };
    }
    const owners = [...new Set(matches.map(m => m.msg.name))];
    const warning = owners.length > 1
      ? `${context}: signal '${sigName}' exists in multiple messages (${owners.join(', ')}); using '${matches[0].msg.name}' — assign the draft via can_msg_get/can_msg_new to disambiguate`
      : null;
    return { sig: matches[0].sig, error: null, warning };
  }

  function raiseFor(message: string): string {
    errors.push(message);
    return `raise("key_error", "${message.replace(/"/g, '\\"')}")`;
  }

  /* ---- Rewrite DBC-aware calls, parenthesised and recursive ---- */
  // Scans an expression/script and rewrites the outermost DBC-aware calls,
  // recursing into their arguments so nested calls are inlined too. Because
  // a call is consumed up to its balanced close paren, rewrites can never
  // overlap.
  const CALL_RE = /\b(can_msg_get|can_msg_new|msg_sig_get|msg_sig_set)\s*\(/;

  function rewrite(text: string): string {
    const re = new RegExp(CALL_RE.source, 'g');
    let out = '';
    let consumed = 0;
    let m: RegExpExecArray | null;

    while ((m = re.exec(text)) !== null) {
      const name = m[1];
      const start = m.index;
      const open = start + m[0].length - 1;
      if (isCommented(text, start)) continue;

      const close = findMatchingParen(text, open);
      if (close < 0) {
        errors.push(`${name}: unbalanced parentheses`);
        break; // leave the remainder untouched rather than emit broken code
      }

      const args = splitTopLevelArgs(text.slice(open + 1, close)).map(a => a.trim());
      let replacement: string | null = null;

      if (name === 'can_msg_get' && args.length === 2 && /^\d+$/.test(args[0])) {
        const msgName = stringLiteralValue(args[1]);
        if (msgName !== null) {
          const msgId = findMsgId(msgName);
          replacement = msgId === null
            ? raiseFor(`can_msg_get: message '${msgName}' not found`)
            : `can_msg_get(${args[0]}, 0x${msgId.toString(16)})`;
        }
      } else if (name === 'can_msg_new' && (args.length === 1 || (args.length === 2 && /^\d+$/.test(args[1])))) {
        const msgName = stringLiteralValue(args[0]);
        if (msgName !== null) {
          const msgId = findMsgId(msgName);
          if (msgId === null) {
            replacement = raiseFor(`can_msg_new: message '${msgName}' not found`);
          } else {
            const msg = messages.find(mm => mm.id === msgId)!;
            replacement = `can_msg_new(0x${msgId.toString(16)}, ${msg.dlc})`;
          }
        }
      } else if (name === 'msg_sig_get' && args.length === 2) {
        const sigName = stringLiteralValue(args[1]);
        if (sigName !== null) {
          const resolved = resolveSignalInDraft(args[0], sigName, 'msg_sig_get');
          if (resolved.warning) warnings.push(resolved.warning);
          replacement = resolved.sig
            ? `msg_sig_get(${rewrite(args[0])}, ${sigMeta(resolved.sig)})`
            : raiseFor(resolved.error!);
        }
      } else if (name === 'msg_sig_set' && args.length === 3) {
        const sigName = stringLiteralValue(args[1]);
        if (sigName !== null) {
          const resolved = resolveSignalInDraft(args[0], sigName, 'msg_sig_set');
          if (resolved.warning) warnings.push(resolved.warning);
          replacement = resolved.sig
            ? `msg_sig_set(${rewrite(args[0])}, ${sigMeta(resolved.sig)}, ${rewrite(args[2])})`
            : raiseFor(resolved.error!);
        }
      }

      out += text.slice(consumed, start);
      out += replacement ?? text.slice(start, close + 1);
      consumed = close + 1;
      re.lastIndex = close + 1;
    }

    out += text.slice(consumed);
    return out;
  }

  const code = rewrite(sourceCode);

  return { code, errors, warnings };
}
