import { parseDbc, type Message } from './parser';

async function sha256(text: string): Promise<string> {
  const enc = new TextEncoder().encode(text);
  const hash = await crypto.subtle.digest('SHA-256', enc);
  const b = new Uint8Array(hash);
  let hex = '';
  for (let i = 0; i < b.length; i++) hex += b[i].toString(16).padStart(2, '0');
  return hex;
}

self.onmessage = async (e: MessageEvent<{ id: number; text: string }>) => {
  const { id, text } = e.data;
  const [messages, hash] = await Promise.all([parseDbc(text), sha256(text)]);
  self.postMessage({ id, messages, hash } satisfies { id: number; messages: Message[]; hash: string });
};
