/**
 * DBC text parser.
 */

// Mux signal flags
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

const MSG_RE = /^BO_\s+(\d+)\s+(\w+)\s*:\s*(\d+)\s+\w+/;
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



/* ------------------------------------------------------------------ */
/*  IndexedDB-backed parse cache + 2-worker pool for concurrency       */
/* ------------------------------------------------------------------ */

let reqId = 0;
const pending = new Map<number, { resolve: (m: Message[]) => void; reject: (e: Error) => void }>();

/* Bump this when the parser or Message/Signal schema changes. */
const CACHE_VER = 1;

/* ---- IndexedDB persistence ---- */

function openDb(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const r = indexedDB.open('DbcCache', 1);
    r.onupgradeneeded = () => {
      if (!r.result.objectStoreNames.contains('dbc'))
        r.result.createObjectStore('dbc');
    };
    r.onsuccess = () => resolve(r.result);
    r.onerror = () => reject(r.error);
  });
}

let dbPromise: Promise<IDBDatabase> | null = null;
function getDb(): Promise<IDBDatabase> {
  if (!dbPromise) dbPromise = openDb();
  return dbPromise;
}

async function persistGet(key: string): Promise<Message[] | undefined> {
  const db = await getDb();
  return await new Promise<Message[] | undefined>((resolve, reject) => {
    const tx = db.transaction('dbc', 'readonly');
    const r = tx.objectStore('dbc').get(key);
    r.onsuccess = () => resolve(r.result ?? undefined);
    r.onerror = () => reject(r.error);
  });
}

async function persistSet(key: string, val: CacheEntry): Promise<void> {
  const db = await getDb();
  await new Promise<void>((resolve, reject) => {
    const tx = db.transaction('dbc', 'readwrite');
    tx.objectStore('dbc').put(val, key);
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
}

async function persistDel(key: string): Promise<void> {
  const db = await getDb();
  await new Promise<void>((resolve, reject) => {
    const tx = db.transaction('dbc', 'readwrite');
    tx.objectStore('dbc').delete(key);
    tx.oncomplete = () => resolve();
    tx.onerror = () => reject(tx.error);
  });
}

/* ---- Worker pool (2 workers for parallel parsing) ---- */

const pool: Worker[] = [];
let poolIdx = 0;

function spawnWorker(): Worker {
  const w = new Worker(new URL('./dbc-parser.worker.ts', import.meta.url), { type: 'module' });
  w.onmessage = (e: MessageEvent<{ id: number; messages: Message[] }>) => {
    const p = pending.get(e.data.id);
    if (p) { pending.delete(e.data.id); p.resolve(e.data.messages); }
  };
  w.onerror = () => {
    for (const [, p] of pending) p.reject(new Error('Worker error'));
    pending.clear();
  };
  return w;
}

function nextWorker(): Worker {
  if (pool.length < 2) pool.push(spawnWorker());
  const w = pool[poolIdx];
  poolIdx = (poolIdx + 1) % pool.length;
  return w;
}

interface CacheEntry {
  hash: string;
  messages: Message[];
}

async function textHash(text: string): Promise<string> {
  const enc = new TextEncoder().encode(text);
  const hash = await crypto.subtle.digest('SHA-256', enc);
  const b = new Uint8Array(hash);
  let hex = '';
  for (let i = 0; i < b.length; i++) hex += b[i].toString(16).padStart(2, '0');
  return hex;
}

export async function parseDbcInWorker(text: string, key?: string): Promise<Message[]> {
  const k = key ?? '';
  const hash = await textHash(text);
  const urlKey = k ? `v${CACHE_VER}:url:${k}` : `v${CACHE_VER}:hash:${hash}`;

  try {
    const entry = await persistGet(urlKey) as CacheEntry | undefined;
    if (entry?.hash === hash) return entry.messages;
    if (entry) await persistDel(urlKey); // stale content, erase
  } catch { /* fall through */ }

  return new Promise((resolve, reject) => {
    const id = ++reqId;
    pending.set(id, {
      resolve: (msgs) => { resolve(msgs); persistSet(urlKey, { hash, messages: msgs } satisfies CacheEntry).catch(() => {}); },
      reject,
    });
    nextWorker().postMessage({ id, text });
  });
}
