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
 * Call matching is parenthesis-balanced, comment-aware and recursive, so
 * DBC-aware calls compose: a msg_sig_get nested inside a msg_sig_set value
 * (e.g. incrementing a counter field) is rewritten as a whole instead of being
 * truncated by the outer match. Berry strings and comments (`#` and `#- … -#`)
 * are skipped, and calls whose shape cannot be resolved are reported in
 * errors[] with a runtime raise rather than passed through silently. Signal
 * names that can only be resolved by picking the first of several matches are
 * reported in warnings[] (non-fatal).
 */

import type { Message, Signal } from '../dbc/parser';

/* ---- Scripting API version ---- */
// The firmware reports scripting_api_version in system.info; the web UI warns
// before uploading a script when versions don't match.
export const SCRIPTING_API_VERSION = 1;

/** Result of preprocessing one script. */
export interface PreprocessResult {
  code: string;
  errors: string[];
  warnings: string[];
}

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

/* Return the last index of a Berry comment starting at `i` (which points at `#`). */
function skipComment(text: string, i: number): number {
  if (text[i + 1] === '-') {
    const end = text.indexOf('-#', i + 2);
    return end < 0 ? text.length - 1 : end + 1;
  }
  const nl = text.indexOf('\n', i);
  return nl < 0 ? text.length - 1 : nl - 1;
}

/* True when `pos` is executable code — not inside a string or comment. */
function isInCode(source: string, pos: number): boolean {
  let inString: string | null = null;
  for (let i = 0; i <= pos; i++) {
    const c = source[i];
    if (inString) {
      if (c === '\\') { i++; continue; }
      if (c === inString) inString = null;
      continue;
    }
    if (c === '"' || c === "'") { inString = c; continue; }
    if (c === '#') {
      const end = skipComment(source, i);
      if (end >= pos) return false;
      i = end;
      continue;
    }
    if (i === pos) return true;
  }
  return false;
}

/* ---- Balanced call scanning ---- */

/**
 * Scan a call starting at its opening `(`, returning the matching `)` and the
 * top-level argument slices. Nested (), [], {} and Berry strings/comments are
 * skipped. Returns null when the parentheses are unbalanced.
 */
function scanArgs(text: string, open: number): { close: number; args: string[] } | null {
  let parenDepth = 0;
  let bracketDepth = 0;
  let inString: string | null = null;
  let start = open + 1;
  const args: string[] = [];
  for (let i = open; i < text.length; i++) {
    const c = text[i];
    if (inString) {
      if (c === '\\') { i++; continue; }
      if (c === inString) inString = null;
      continue;
    }
    if (c === '"' || c === "'") { inString = c; continue; }
    if (c === '#') { i = skipComment(text, i); continue; }
    if (c === '(') parenDepth++;
    else if (c === ')') {
      parenDepth--;
      if (parenDepth === 0) {
        args.push(text.slice(start, i));
        return { close: i, args };
      }
    } else if (c === '[' || c === '{') bracketDepth++;
    else if (c === ']' || c === '}') bracketDepth--;
    else if (c === ',' && parenDepth === 1 && bracketDepth === 0) {
      args.push(text.slice(start, i));
      start = i + 1;
    }
  }
  return null;
}

type SingleString =
  | { kind: 'literal'; value: string }
  | { kind: 'code' }
  | { kind: 'unterminated' };

/* Classify an expression: exactly one plain string literal, other code, or an open quote. */
function parseSingleString(expr: string): SingleString {
  const t = expr.trim();
  if (t.length === 0) return { kind: 'code' };
  const quote = t[0];
  if (quote !== '"' && quote !== "'") return { kind: 'code' };
  let value = '';
  let i = 1;
  for (; i < t.length; i++) {
    const c = t[i];
    if (c === '\\') { value += t[++i] ?? ''; continue; }
    if (c === quote) break;
    value += c;
  }
  if (i >= t.length) return { kind: 'unterminated' };
  if (t.slice(i + 1).trim() !== '') return { kind: 'code' };
  return { kind: 'literal', value };
}

/* True when an expression starts with a string token but is not a single literal. */
function startsWithString(expr: string): boolean {
  const t = expr.trim();
  return t.startsWith('"') || t.startsWith("'");
}

/* First plain string literal among a call's arguments, or null. */
function messageNameFromArgs(args: string[]): string | null {
  for (const arg of args) {
    const literal = parseSingleString(arg);
    if (literal.kind === 'literal') return literal.value;
  }
  return null;
}

/* ---- Track variable → message name mappings ---- */
// Scans source for var X = can_msg_get(...) / can_msg_new(...) assignments,
// reusing the same call scanner as the rewriter. Returns a map of variable
// name → set of message types it was assigned from (may be >1, indicating a
// reused variable).
export function trackVarToMsgs(source: string): Map<string, Set<string>> {
  const varToMsgs = new Map<string, Set<string>>();
  const assignmentRe = /(?:^|\s)(?:var\s+)?(\w+)\s*=\s*(can_msg_get|can_msg_new)\s*\(/g;
  let m: RegExpExecArray | null;
  while ((m = assignmentRe.exec(source)) !== null) {
    if (!isInCode(source, m.index)) continue;
    const call = scanArgs(source, m.index + m[0].length - 1);
    if (!call) continue;
    const msgName = messageNameFromArgs(call.args);
    if (msgName === null) continue;
    const varName = m[1];
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
): PreprocessResult {
  const errors: string[] = [];
  const warnings: string[] = [];

  const msgByName = new Map<string, Message>();
  const msgsBySignal = new Map<string, Array<{ msg: Message; sig: Signal }>>();
  for (const msg of messages) {
    if (!msgByName.has(msg.name)) msgByName.set(msg.name, msg);
    for (const sig of msg.signals) {
      const list = msgsBySignal.get(sig.name);
      if (list) list.push({ msg, sig });
      else msgsBySignal.set(sig.name, [{ msg, sig }]);
    }
  }

  function findSigInMsg(msgName: string, sigName: string): Signal | null {
    return msgByName.get(msgName)?.signals.find(s => s.name === sigName) ?? null;
  }

  function findMsg(msgName: string): Message | null {
    return msgByName.get(msgName) ?? null;
  }

  /* All message entries that contain a signal of this name. */
  function signalMatches(sigName: string): Array<{ msg: Message; sig: Signal }> {
    return msgsBySignal.get(sigName) ?? [];
  }

  const varToMsgs = trackVarToMsgs(sourceCode);

  /* The message a draft expression comes from, when it can be determined. */
  function draftMessageName(draftExpr: string): string | null {
    const expr = draftExpr.trim();
    if (/^\w+$/.test(expr)) {
      const names = varToMsgs.get(expr);
      return names && names.size === 1 ? [...names][0] : null;
    }
    const call = /^(can_msg_get|can_msg_new)\s*\(/.exec(expr);
    if (!call) return null;
    const scanned = scanArgs(expr, call[0].length - 1);
    if (!scanned || scanned.close !== expr.length - 1) return null;
    return messageNameFromArgs(scanned.args);
  }

  type ResolveResult =
    | { ok: true; sig: Signal; warning: string | null }
    | { ok: false; error: string };

  function resolveSignalInDraft(
    draftExpr: string,
    sigName: string,
    context: string
  ): ResolveResult {
    const ownerName = draftMessageName(draftExpr);

    if (ownerName) {
      const sig = findSigInMsg(ownerName, sigName);
      if (sig) return { ok: true, sig, warning: null };

      // Signal not in the owning message — report where it does live.
      const owners = [...new Set(signalMatches(sigName).map(m => m.msg.name))]
        .filter(n => n !== ownerName);
      if (owners.length === 1) {
        return {
          ok: false,
          error: `${context}: signal '${sigName}' belongs to '${owners[0]}', not '${ownerName}'`,
        };
      }
      if (owners.length > 1) {
        return {
          ok: false,
          error: `${context}: signal '${sigName}' is not in message '${ownerName}', but is present in multiple messages (${owners.join(', ')})`,
        };
      }
      return {
        ok: false,
        error: `${context}: signal '${sigName}' not found in message '${ownerName}'`,
      };
    }

    // Untracked or multi-assigned variable — fall back to global search.
    const matches = signalMatches(sigName);
    if (matches.length === 0) {
      return { ok: false, error: `${context}: signal '${sigName}' not found` };
    }
    const owners = [...new Set(matches.map(m => m.msg.name))];
    const warning = owners.length > 1
      ? `${context}: signal '${sigName}' exists in multiple messages (${owners.join(', ')}); using '${matches[0].msg.name}' — assign the draft via can_msg_get/can_msg_new to disambiguate`
      : null;
    return { ok: true, sig: matches[0].sig, warning };
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
  function rewrite(text: string): string {
    const re = /\b(can_msg_get|can_msg_new|msg_sig_get|msg_sig_set)\s*\(/g;
    let out = '';
    let consumed = 0;
    let m: RegExpExecArray | null;

    while ((m = re.exec(text)) !== null) {
      const name = m[1];
      const start = m.index;
      const open = start + m[0].length - 1;
      if (!isInCode(text, start)) continue;

      const call = scanArgs(text, open);
      if (!call) {
        errors.push(`${name}: unbalanced parentheses`);
        continue; // keep scanning: one bad call must not disable the rest
      }

      const { close, args } = call;
      const draft = (args[0] ?? '').trim();
      const value = (args[2] ?? '').trimStart();
      let replacement: string | null = null;

      if (name === 'can_msg_get') {
        if (args.length !== 2) {
          replacement = raiseFor('can_msg_get: expected 2 arguments');
        } else {
          const msgName = parseSingleString(args[1]);
          if (msgName.kind === 'literal') {
            const msg = findMsg(msgName.value);
            replacement = msg === null
              ? raiseFor(`can_msg_get: message '${msgName.value}' not found`)
              : `can_msg_get(${rewrite(draft)}, 0x${msg.id.toString(16)})`;
          } else if (msgName.kind === 'unterminated') {
            replacement = raiseFor('can_msg_get: unterminated message name');
          } else if (startsWithString(args[1])) {
            replacement = raiseFor('can_msg_get: message name must be a compile-time string literal');
          } else if (parseSingleString(args[0]).kind === 'literal') {
            replacement = raiseFor('can_msg_get: message name must be the second argument (bus first)');
          }
        }
      } else if (name === 'can_msg_new') {
        if (args.length > 2) {
          replacement = raiseFor('can_msg_new: expected 1 or 2 arguments');
        } else {
          let msgName: string | null = null;
          let badLiteral = false;
          for (const arg of args) {
            const literal = parseSingleString(arg);
            if (literal.kind === 'literal') msgName ??= literal.value;
            else if (literal.kind === 'unterminated' || startsWithString(arg)) badLiteral = true;
          }
          if (badLiteral) {
            replacement = raiseFor('can_msg_new: message name must be a compile-time string literal');
          } else if (msgName !== null) {
            const msg = findMsg(msgName);
            replacement = msg === null
              ? raiseFor(`can_msg_new: message '${msgName}' not found`)
              : `can_msg_new(0x${msg.id.toString(16)}, ${msg.dlc})`;
          }
        }
      } else if (name === 'msg_sig_get') {
        if (args.length === 7) {
          // Already inlined by a previous preprocess pass.
        } else if (args.length !== 2) {
          replacement = raiseFor('msg_sig_get: expected 2 arguments (or 7 for the inline form)');
        } else {
          const sigName = parseSingleString(args[1]);
          if (sigName.kind === 'unterminated') {
            replacement = raiseFor('msg_sig_get: unterminated signal name');
          } else if (sigName.kind === 'code') {
            replacement = raiseFor('msg_sig_get: signal name must be a compile-time string literal');
          } else {
            const resolved = resolveSignalInDraft(draft, sigName.value, 'msg_sig_get');
            if (!resolved.ok) {
              replacement = raiseFor(resolved.error);
            } else {
              if (resolved.warning) warnings.push(resolved.warning);
              replacement = `msg_sig_get(${rewrite(draft)}, ${sigMeta(resolved.sig)})`;
            }
          }
        }
      } else if (name === 'msg_sig_set') {
        if (args.length === 8) {
          // Already inlined by a previous preprocess pass.
        } else if (args.length !== 3) {
          replacement = raiseFor('msg_sig_set: expected 3 arguments (or 8 for the inline form)');
        } else {
          const sigName = parseSingleString(args[1]);
          if (sigName.kind === 'unterminated') {
            replacement = raiseFor('msg_sig_set: unterminated signal name');
          } else if (sigName.kind === 'code') {
            replacement = raiseFor('msg_sig_set: signal name must be a compile-time string literal');
          } else {
            const resolved = resolveSignalInDraft(draft, sigName.value, 'msg_sig_set');
            if (!resolved.ok) {
              replacement = raiseFor(resolved.error);
            } else {
              if (resolved.warning) warnings.push(resolved.warning);
              replacement = `msg_sig_set(${rewrite(draft)}, ${sigMeta(resolved.sig)}, ${rewrite(value)})`;
            }
          }
        }
      }

      if (replacement === null) continue; // leave unrecognised calls for inner rewrites
      out += text.slice(consumed, start);
      out += replacement;
      consumed = close + 1;
      re.lastIndex = close + 1;
    }

    out += text.slice(consumed);
    return out;
  }

  const code = rewrite(sourceCode);

  return { code, errors, warnings: [...new Set(warnings)] };
}
