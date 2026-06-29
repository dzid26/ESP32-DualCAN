/**
 * Berry script preprocessor: converts `.be` source to `.bep` runtime format.
 *
 * Design: Self-contained preprocessed scripts with NO runtime DBC dependency.
 * All signal metadata is inlined as Berry code by the preprocessor.
 *
 * Conversion rules:
 *   can_msg_get(bus, "msg")         → can_msg_get(bus, 0xID)
 *   can_msg_new("msg")              → can_msg_new(0xID, dlc)
 *   msg_sig_get(draft, "sig")    → msg_sig_get(draft, sb, len, be, signed, scale, offset)
 *   msg_sig_set(draft, "sig", val)  → msg_sig_set(draft, sb, len, be, signed, scale, offset, val)
 * Missing signals/messages → raise("key_error", "...")
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

/* ---- Replace-at helper (end-to-start offset-safe) ---- */

function replaceAt(str: string, start: number, end: number, replacement: string): string {
  return str.substring(0, start) + replacement + str.substring(end);
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
    const varName = m[1];
    const msgName = m[2];
    if (!varToMsgs.has(varName)) varToMsgs.set(varName, new Set());
    varToMsgs.get(varName)!.add(msgName);
  }
  const varNewRe = /(?:^|\s)(?:var\s+)?(\w+)\s*=\s*can_msg_new\s*\(\s*"([^"]+)"\s*(?:,\s*\d+\s*)?\)/g;
  while ((m = varNewRe.exec(source)) !== null) {
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
): { code: string; errors: string[] } {
  const errors: string[] = [];
  let code = sourceCode;

  const replacements: Array<{ start: number; end: number; replacement: string }> = [];
  const missingMessages = new Set<string>();
  const missingSignals = new Set<string>();

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

  function findMsgContainingSignal(sigName: string): Message | null {
    for (const msg of messages) {
      for (const sig of msg.signals) {
        if (sig.name === sigName) return msg;
      }
    }
    return null;
  }

  const varToMsgs = trackVarToMsgs(sourceCode);

  function resolveSignalInDraft(
    draftExpr: string,
    sigName: string,
    context: string
  ): { sig: Signal | null; error: string | null } {
    const isSimpleVar = /^\w+$/.test(draftExpr);
    const msgNames = isSimpleVar ? varToMsgs.get(draftExpr) : undefined;
    const uniqueMsgName = msgNames && msgNames.size === 1 ? msgNames.values().next().value : null;

    if (uniqueMsgName) {
      // We know exactly which message this variable belongs to
      const found = findSigInMsg(uniqueMsgName, sigName);
      if (found) return { sig: found.sig, error: null };

      // Signal not found in expected message — check if it belongs elsewhere
      const owner = findMsgContainingSignal(sigName);
      if (owner && owner.name !== uniqueMsgName) {
        return {
          sig: null,
          error: `${context}: signal '${sigName}' belongs to '${owner.name}', not '${uniqueMsgName}'`
        };
      }
      return {
        sig: null,
        error: `${context}: signal '${sigName}' not found in message '${uniqueMsgName}'`
      };
    }

    // Untracked or multi-assigned variable — fall back to global search
    const sig = findSignalByName(sigName);
    if (!sig) return { sig: null, error: `${context}: signal '${sigName}' not found` };
    return { sig, error: null };
  }

  /* ---- Pattern 1: can_msg_get(bus, "msg") ---- */
  // Bus is always first and required. Rewrites to can_msg_get(bus, 0xID)
  // which returns a draft map {id, data, dlc} via the C binding.
  const canMsgGetPattern = /can_msg_get\s*\(\s*(\d+)\s*,\s*"([^"]+)"\s*\)/g;
  let match: RegExpExecArray | null;
  while ((match = canMsgGetPattern.exec(code)) !== null) {
    const bus = match[1];
    const msgName = match[2];
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

    const replacement = `can_msg_get(${bus}, 0x${msgId.toString(16)})`;
    replacements.push({ start: matchStart, end: matchEnd, replacement });
  }

  /* ---- Pattern 1b: can_msg_new("msg" [, bus]) ---- */
  // Resolves message name to ID + DLC; emits can_msg_new(0xID, dlc)
  // which returns a draft map {id, data, dlc} with zeroed data.
  const canMsgNewNamePattern = /can_msg_new\s*\(\s*"([^"]+)"\s*((?:,\s*\d+\s*)?)\)/g;
  while ((match = canMsgNewNamePattern.exec(code)) !== null) {
    const msgName = match[1];
    const fullMatch = match[0];
    const matchStart = match.index;
    const matchEnd = matchStart + fullMatch.length;

    const msgId = findMsgId(msgName);
    if (msgId === null) {
      missingMessages.add(msgName);
      errors.push(`can_msg_new: message '${msgName}' not found`);
      const replacement = `raise("key_error", "can_msg_new: message '${msgName}' not found")`;
      replacements.push({ start: matchStart, end: matchEnd, replacement });
      continue;
    }

    const msg = messages.find(m => m.id === msgId)!;
    const replacement = `can_msg_new(0x${msgId.toString(16)}, ${msg.dlc})`;
    replacements.push({ start: matchStart, end: matchEnd, replacement });
  }

  /* ---- Pattern 2: msg_sig_get(draft, "sig") -> msg_sig_get(draft, sb, len, be, signed, scale, offset) ---- */
  // Decodes a DBC signal from a message draft. The native C binding reads
  // draft.data internally and handles scale, offset, signedness, byte order.
  const canSigGetPattern = /msg_sig_get\s*\(\s*([^,]+)\s*,\s*"([^"]+)"\s*\)/g;
  while ((match = canSigGetPattern.exec(code)) !== null) {
    const draftExpr = match[1].trim();
    const sigName = match[2];
    const fullMatch = match[0];
    const matchStart = match.index;
    const matchEnd = matchStart + fullMatch.length;

    const resolved = resolveSignalInDraft(draftExpr, sigName, 'msg_sig_get');
    if (!resolved.sig) {
      missingSignals.add(sigName);
      errors.push(resolved.error!);
      const replacement = `raise("key_error", "${resolved.error!.replace(/"/g, '\\"')}")`;
      replacements.push({ start: matchStart, end: matchEnd, replacement });
      continue;
    }

    const replacement = `msg_sig_get(${draftExpr}, ${sigMeta(resolved.sig)})`;
    replacements.push({ start: matchStart, end: matchEnd, replacement });
  }

  /* ---- Pattern 4: msg_sig_set(draft, "sig", value) -> msg_sig_set(draft, sb, len, be, signed, scale, offset, val) ---- */
  // Encodes a physical value into a message draft. The native C binding
  // writes draft.data in-place and handles scale, offset, signedness, byte order.
  const canMsgSetPattern =
    /msg_sig_set\s*\(\s*([^,]+)\s*,\s*"([^"]+)"\s*,\s*([^)]+)\)/g;
  while ((match = canMsgSetPattern.exec(code)) !== null) {
    const draftExpr = match[1].trim();
    const sigName = match[2];
    const valueExpr = match[3].trim();
    const fullMatch = match[0];
    const matchStart = match.index;
    const matchEnd = matchStart + fullMatch.length;

    const resolved = resolveSignalInDraft(draftExpr, sigName, 'msg_sig_set');
    if (!resolved.sig) {
      missingSignals.add(sigName);
      errors.push(resolved.error!);
      const replacement = `raise("key_error", "${resolved.error!.replace(/"/g, '\\"')}")`;
      replacements.push({ start: matchStart, end: matchEnd, replacement });
      continue;
    }

    const replacement = `msg_sig_set(${draftExpr}, ${sigMeta(resolved.sig)}, ${valueExpr})`;
    replacements.push({ start: matchStart, end: matchEnd, replacement });
  }

  /* ---- Apply replacements end-to-start ---- */
  replacements.sort((a, b) => b.start - a.start);
  for (const r of replacements) {
    code = replaceAt(code, r.start, r.end, r.replacement);
  }

  return { code, errors };
}
