/**
 * Berry script preprocessor: converts `.be` source to `.bep` runtime format.
 *
 * Design: Self-contained preprocessed scripts with NO runtime DBC dependency.
 * All signal metadata is inlined as Berry code by the preprocessor.
 *
 * Conversion rules:
 *   can_msg_get("msg" [, bus])      → can_msg_get(0xID [, bus])
 *   can_signal_get("msg", "sig" [, bus])
 *     → __sig_get(msg_id, sb, len, be, sg, sc, off [, bus])
 *   on_can_signal("msg", "sig", fn [, bus])
 *     → __watch_sig(msg_id, sb, len, be, sg, sc, off [, bus], fn)
 *   can_msg_set(draft, "sig", value)
 *     → draft.data = signal_encode(draft.data, sb, len, be, sg, sc, off, value)
 * Missing signals/messages → raise("key_error", "...")
 */

import type { Message, Signal } from '../dbc/parser';

/* Build a Berry signal-metadata key from a Signal. */
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

/* ---- Helper-function generation ---- */

let helperSigGet: string | null = null;
let helperWatchSig: string | null = null;
let helperTimer: string | null = null;

function ensureHelperSigGet(): void {
  if (helperSigGet) return;
  helperSigGet = [
    'def __sig_get(msg_id, sb, len, be, sg, sc, off, bus)',
    '  var f = can_msg_get(msg_id, bus)',
    '  if f != nil',
    '    return signal_decode(f.data, sb, len, be, sg, sc, off)',
    '  end',
    '  return nil',
    'end',
  ].join('\n');
}

function ensureHelperWatchSig(): void {
  if (helperWatchSig) return;
  helperWatchSig = [
    'def __watch_sig(msg_id, sb, len, be, sg, sc, off, bus, fn)',
    '  var _key = str(msg_id) + ":" + str(sb) + ":" + str(len)',
    '  if var __subs && __subs.find(_key) return end',
    '  if !var __subs __subs = {} end',
    '  __subs[_key] = {prev: nil, fn: fn, msg_id: msg_id, sb: sb, len: len, be: be, sg: sg, sc: sc, off: off, bus: bus}',
    '  if !var __poll_timer',
    '    __poll_timer = timer.every(50, def()',
    '      for var k : __subs.keys()',
    '        var s = __subs[k]',
    '        var f = can_msg_get(s.msg_id, s.bus)',
    '        if f != nil',
    '          var v = signal_decode(f.data, s.sb, s.len, s.be, s.sg, s.sc, s.off)',
    '          if v != s.prev',
    '            s.fn({value: v, prev: s.prev, changed: s.prev != nil})',
    '            s.prev = v',
    '          end',
    '        end',
    '      end',
    '    end)',
    '  end',
    'end',
  ].join('\n');
}

function buildHelpers(): string {
  const parts: string[] = [];
  if (helperSigGet) parts.push(helperSigGet);
  if (helperWatchSig) parts.push(helperWatchSig);
  return parts.length ? '\n\n' + parts.join('\n\n') + '\n\n' : '';
}

/* ---- Replace-at helper (end-to-start offset-safe) ---- */

function replaceAt(str: string, start: number, end: number, replacement: string): string {
  return str.substring(0, start) + replacement + str.substring(end);
}

/* ---- Main entry point ---- */

export function preprocessScript(
  sourceCode: string,
  messages: Message[]
): { code: string; errors: string[] } {
  const errors: string[] = [];
  let code = sourceCode;

  const replacements: Array<{ start: number; end: number; replacement: string }> = [];
  const missingSignals = new Set<string>();
  const missingMessages = new Set<string>();

  helperSigGet = null;
  helperWatchSig = null;

  /* ---- Helpers to look up signals and messages ---- */

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

  function findSignalByName(sigName: string): Signal | null {
    for (const msg of messages) {
      for (const sig of msg.signals) {
        if (sig.name === sigName) return sig;
      }
    }
    return null;
  }

  /* ---- Pattern 1: can_msg_get("msg" [, bus]) ---- */
  // Matches: can_msg_get("msg") or can_msg_get("msg", bus)
  const canMsgGetPattern = /can_msg_get\s*\(\s*"([^"]+)"\s*((?:,\s*\d+\s*)?)\)/g;
  let match: RegExpExecArray | null;
  while ((match = canMsgGetPattern.exec(code)) !== null) {
    const msgName = match[1];
    const trailing = match[2]; // e.g. ", 1" or ""
    const fullMatch = match[0];
    const matchStart = match.index;
    const matchEnd = matchStart + fullMatch.length;

    const msgId = findMsgId(msgName);
    if (msgId === null) {
      missingMessages.add(msgName);
      errors.push(`can_msg_get: message '${msgName}' not found`);
      const replacement = `raise("key_error", "can_msg_get: message '${msgName}' not found")`;
      replacements.push({ start: matchStart, end: matchEnd, replacement });
      continue;
    }

    const replacement = `can_msg_get(0x${msgId.toString(16)}${trailing})`;
    replacements.push({ start: matchStart, end: matchEnd, replacement });
  }

  /* ---- Pattern 2: can_signal_get("msg", "sig" [, bus]) ---- */
  const canSigGetPattern =
    /can_signal_get\s*\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*((?:,\s*\d+\s*)?)\)/g;
  while ((match = canSigGetPattern.exec(code)) !== null) {
    const msgName = match[1];
    const sigName = match[2];
    const trailing = match[3]; // ", bus" or ""
    const fullMatch = match[0];
    const matchStart = match.index;
    const matchEnd = matchStart + fullMatch.length;

    const ref = findSigInMsg(msgName, sigName);
    if (!ref) {
      missingSignals.add(`${msgName}.${sigName}`);
      errors.push(`can_signal_get: signal '${msgName}.${sigName}' not found`);
      const replacement = `raise("key_error", "can_signal_get: signal '${msgName}.${sigName}' not found")`;
      replacements.push({ start: matchStart, end: matchEnd, replacement });
      continue;
    }

    ensureHelperSigGet();
    const busArg = trailing ? trailing.trim() : '0';
    const replacement = `__sig_get(0x${ref.msg.id.toString(16)}, ${sigMeta(ref.sig)}, ${busArg})`;
    replacements.push({ start: matchStart, end: matchEnd, replacement });
  }

  /* ---- Pattern 3: on_can_signal("msg", "sig", fn [, bus]) ---- */
  // Matches: on_can_signal("msg", "sig", fn) or on_can_signal("msg", "sig", fn, bus)
  const onCanSignalPattern =
    /on_can_signal\s*\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*([^,]+)\s*((?:,\s*\d+\s*)?)\)/g;
  while ((match = onCanSignalPattern.exec(code)) !== null) {
    const msgName = match[1];
    const sigName = match[2];
    const fnExpr = match[3].trim();
    const trailing = match[4]; // ", bus" or ""
    const fullMatch = match[0];
    const matchStart = match.index;
    const matchEnd = matchStart + fullMatch.length;

    const ref = findSigInMsg(msgName, sigName);
    if (!ref) {
      missingSignals.add(`${msgName}.${sigName}`);
      errors.push(`on_can_signal: signal '${msgName}.${sigName}' not found`);
      const replacement = `raise("key_error", "on_can_signal: signal '${msgName}.${sigName}' not found")`;
      replacements.push({ start: matchStart, end: matchEnd, replacement });
      continue;
    }

    ensureHelperWatchSig();
    const busArg = trailing ? trailing.trim() : '0';
    const replacement = `__watch_sig(0x${ref.msg.id.toString(16)}, ${sigMeta(ref.sig)}, ${busArg}, ${fnExpr})`;
    replacements.push({ start: matchStart, end: matchEnd, replacement });
  }

  /* ---- Pattern 4: can_msg_set(draft, "sig", value) ---- */
  const canMsgSetPattern =
    /can_msg_set\s*\(\s*([^,]+)\s*,\s*"([^"]+)"\s*,\s*([^)]+)\)/g;
  while ((match = canMsgSetPattern.exec(code)) !== null) {
    const draftExpr = match[1].trim();
    const sigName = match[2];
    const valueExpr = match[3].trim();
    const fullMatch = match[0];
    const matchStart = match.index;
    const matchEnd = matchStart + fullMatch.length;

    const sig = findSignalByName(sigName);
    if (!sig) {
      missingSignals.add(sigName);
      errors.push(`can_msg_set: signal '${sigName}' not found`);
      const replacement = `raise("key_error", "can_msg_set: signal '${sigName}' not found")`;
      replacements.push({ start: matchStart, end: matchEnd, replacement });
      continue;
    }

    const replacement = `${draftExpr}["data"] = signal_encode(${draftExpr}["data"], ${sigMeta(sig)}, ${valueExpr})`;
    replacements.push({ start: matchStart, end: matchEnd, replacement });
  }

  /* ---- Apply replacements end-to-start ---- */
  replacements.sort((a, b) => b.start - a.start);
  for (const r of replacements) {
    code = replaceAt(code, r.start, r.end, r.replacement);
  }

  /* ---- Prepend Berry helper functions if needed ---- */
  const helpers = buildHelpers();
  if (helpers) {
    code = code + helpers;
  }

  return { code, errors };
}
