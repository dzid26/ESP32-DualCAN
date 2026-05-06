/**
 * DBC text parser → binary compiler.
 * Parses standard DBC format and produces the binary blob that the
 * firmware's dbc_load() can consume directly.
 *
 * Binary format matches firmware/src/dbc/dbc_types.h.
 */

const DBC_MAGIC = [0x44, 0x42, 0x43, 0x00]; // "DBC\0"
const DBC_VERSION = 1;

// Signal flags (must match dbc_types.h)
const SIG_SIGNED = 1 << 0;
const SIG_BIG_ENDIAN = 1 << 1;
const SIG_MUX_NONE = 0 << 2;
const SIG_MUX_SELECTOR = 1 << 2;
const SIG_MUX_MUXED = 2 << 2;

export interface Signal {
  name: string;
  startBit: number;
  bitLength: number;
  isBigEndian: boolean;
  isSigned: boolean;
  scale: number;
  offset: number;
  muxType: number;
  muxValue: number;
}

export interface Message {
  id: number;
  name: string;
  dlc: number;
  signals: Signal[];
}

const MSG_RE = /^BO_\s+(\d+)\s+(\w+)\s*:\s*(\d+)\s+(\w+)/;
const SIG_RE =
  /^\s+SG_\s+(\w+)\s+(M|m\d+)?\s*:\s*(\d+)\|(\d+)@([01])([+-])\s*\(([^,]+),([^)]+)\)\s*\[([^|]+)\|([^\]]+)\]\s*"([^"]*)"\s*(.+)/;

export function parseDbc(text: string): Message[] {
  const messages: Message[] = [];
  let current: Message | null = null;

  for (const line of text.split('\n')) {
    const mm = MSG_RE.exec(line);
    if (mm) {
      current = {
        id: parseInt(mm[1]),
        name: mm[2],
        dlc: parseInt(mm[3]),
        signals: [],
      };
      messages.push(current);
      continue;
    }

    const ms = SIG_RE.exec(line);
    if (ms && current) {
      const muxInd = ms[2] || '';
      let muxType = SIG_MUX_NONE;
      let muxValue = 0;
      if (muxInd === 'M') {
        muxType = SIG_MUX_SELECTOR;
      } else if (muxInd.startsWith('m')) {
        muxType = SIG_MUX_MUXED;
        muxValue = parseInt(muxInd.slice(1));
      }

      current.signals.push({
        name: ms[1],
        startBit: parseInt(ms[3]),
        bitLength: parseInt(ms[4]),
        isBigEndian: ms[5] === '0',
        isSigned: ms[6] === '-',
        scale: parseFloat(ms[7]),
        offset: parseFloat(ms[8]),
        muxType,
        muxValue,
      });
    }
  }

  messages.sort((a, b) => a.id - b.id);
  return messages;
}

export function compileDbc(messages: Message[], busId: number = 0): Uint8Array {
  // Build string table
  const strings = new Map<string, number>();
  const strBytes: number[] = [];

  function intern(s: string): number {
    if (strings.has(s)) return strings.get(s)!;
    const off = strBytes.length;
    strings.set(s, off);
    const enc = new TextEncoder().encode(s);
    strBytes.push(...enc, 0);
    return off;
  }

  for (const msg of messages) {
    intern(msg.name);
    for (const sig of msg.signals) intern(sig.name);
  }

  // Count totals
  const totalSigs = messages.reduce((n, m) => n + m.signals.length, 0);

  // Sizes
  const HDR_SIZE = 28;
  const MSG_SIZE = 12;
  const SIG_SIZE = 16;
  const msgsOffset = HDR_SIZE;
  const sigsOffset = msgsOffset + messages.length * MSG_SIZE;
  const strsOffset = sigsOffset + totalSigs * SIG_SIZE;
  const totalSize = strsOffset + strBytes.length;

  const buf = new ArrayBuffer(totalSize);
  const view = new DataView(buf);
  const u8 = new Uint8Array(buf);
  let pos = 0;

  // Header
  u8.set(DBC_MAGIC, 0);
  view.setUint8(4, DBC_VERSION);
  view.setUint8(5, busId);
  view.setUint16(6, messages.length, true);
  view.setUint16(8, totalSigs, true);
  view.setUint16(10, 0, true); // reserved
  view.setUint32(12, msgsOffset, true);
  view.setUint32(16, sigsOffset, true);
  view.setUint32(20, strsOffset, true);
  view.setUint32(24, strBytes.length, true);

  // Messages
  pos = msgsOffset;
  let sigIdx = 0;
  for (const msg of messages) {
    view.setUint32(pos, msg.id, true);
    view.setUint16(pos + 4, intern(msg.name), true);
    view.setUint8(pos + 6, msg.dlc);
    view.setUint8(pos + 7, msg.signals.length);
    view.setUint16(pos + 8, sigIdx, true);
    view.setUint16(pos + 10, 0, true); // pad
    pos += MSG_SIZE;
    sigIdx += msg.signals.length;
  }

  // Signals
  pos = sigsOffset;
  for (const msg of messages) {
    for (const sig of msg.signals) {
      let flags = 0;
      if (sig.isSigned) flags |= SIG_SIGNED;
      if (sig.isBigEndian) flags |= SIG_BIG_ENDIAN;
      flags |= sig.muxType;

      view.setUint16(pos, intern(sig.name), true);
      view.setUint8(pos + 2, sig.startBit);
      view.setUint8(pos + 3, sig.bitLength);
      view.setUint8(pos + 4, flags);
      view.setUint8(pos + 5, sig.muxValue);
      view.setUint16(pos + 6, 0, true); // pad
      view.setFloat32(pos + 8, sig.scale, true);
      view.setFloat32(pos + 12, sig.offset, true);
      pos += SIG_SIZE;
    }
  }

  // String table
  u8.set(strBytes, strsOffset);

  return new Uint8Array(buf);
}
