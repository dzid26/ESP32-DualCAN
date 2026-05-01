import type { Transport } from './types';

/**
 * JSON-over-BLE protocol client.
 *
 * Wire format: 4-byte little-endian length prefix + UTF-8 JSON payload.
 * BLE fragments may split a frame; the receiver buffers until complete.
 */

export interface ScriptInfo {
  filename: string;
  enabled: boolean;
  errored: boolean;
  error?: string;
}

export interface SignalValue {
  value: number;
  prev: number;
  changed: boolean;
}

type Pending = {
  resolve: (value: any) => void;
  reject: (err: Error) => void;
};

export class Protocol {
  private transport: Transport;
  private rxBuf = new Uint8Array(0);
  private pending = new Map<number, Pending>();
  private nextId = 1;
  private encoder = new TextEncoder();
  private decoder = new TextDecoder();
  private logCallback: ((msg: string) => void) | null = null;

  /** Register a callback for Berry print() log lines pushed from the device. */
  onLog(cb: (msg: string) => void) { this.logCallback = cb; }

  // Conservative chunk size for BLE writes. Real MTU is usually higher
  // after negotiation, but Web Bluetooth doesn't expose it. 100 B works
  // on every stack we've tested.
  private readonly CHUNK = 100;

  constructor(transport: Transport) {
    this.transport = transport;
    transport.onReceive((data) => this.onRx(data));
  }

  async call<T = any>(op: string, params: Record<string, unknown> = {}): Promise<T> {
    if (!this.transport.connected) throw new Error('Not connected');

    const id = this.nextId++;
    const reqBytes = this.encoder.encode(JSON.stringify({ op, id, ...params }));
    const frame = new Uint8Array(4 + reqBytes.length);
    const len = reqBytes.length;
    frame[0] = len & 0xff;
    frame[1] = (len >> 8) & 0xff;
    frame[2] = (len >> 16) & 0xff;
    frame[3] = (len >> 24) & 0xff;
    frame.set(reqBytes, 4);

    const promise = new Promise<T>((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      setTimeout(() => {
        if (this.pending.delete(id)) {
          reject(new Error(`Timeout waiting for response to ${op}`));
        }
      }, 5000);
    });

    for (let off = 0; off < frame.length; off += this.CHUNK) {
      const chunk = frame.subarray(off, Math.min(off + this.CHUNK, frame.length));
      await this.transport.send(chunk);
    }

    return promise;
  }

  private onRx(data: Uint8Array) {
    const merged = new Uint8Array(this.rxBuf.length + data.length);
    merged.set(this.rxBuf);
    merged.set(data, this.rxBuf.length);
    this.rxBuf = merged;

    while (this.rxBuf.length >= 4) {
      const jlen =
        this.rxBuf[0] |
        (this.rxBuf[1] << 8) |
        (this.rxBuf[2] << 16) |
        (this.rxBuf[3] << 24);
      if (this.rxBuf.length < 4 + jlen) return;

      const payload = this.rxBuf.subarray(4, 4 + jlen);
      const text = this.decoder.decode(payload);
      this.rxBuf = this.rxBuf.subarray(4 + jlen);

      try {
        const resp = JSON.parse(text);
        // Push frame: firmware-initiated, no id, has a type field.
        if (resp.type !== undefined) {
          if (resp.type === 'log' && this.logCallback) this.logCallback(resp.msg ?? '');
          continue;
        }
        const p = this.pending.get(resp.id);
        if (!p) {
          console.warn('Unsolicited response', resp);
          continue;
        }
        this.pending.delete(resp.id);
        if (resp.ok) p.resolve(resp.result ?? null);
        else p.reject(new Error(resp.error || 'Request failed'));
      } catch (e) {
        console.error('Bad frame', text, e);
      }
    }
  }

  // ---- high-level convenience methods ----

  ping(): Promise<string> {
    return this.call('ping');
  }

  listScripts(): Promise<{ scripts: ScriptInfo[] }> {
    return this.call('script.list');
  }

  writeScript(filename: string, code: string): Promise<void> {
    return this.call('script.write', { filename, code });
  }

  readScript(filename: string): Promise<{ code: string }> {
    return this.call('script.read', { filename });
  }

  enableScript(filename: string): Promise<void> {
    return this.call('script.enable', { filename });
  }

  disableScript(filename: string): Promise<void> {
    return this.call('script.disable', { filename });
  }

  deleteScript(filename: string): Promise<void> {
    return this.call('script.delete', { filename });
  }

  listActions(): Promise<{ actions: string[] }> {
    return this.call('action.list');
  }

  invokeAction(name: string): Promise<void> {
    return this.call('action.invoke', { name });
  }

  /** Returns current signal state, or null if the signal is not found / no DBC loaded. */
  getSignalValue(name: string, bus = 0): Promise<SignalValue | null> {
    return this.call('signal.value', { name, bus });
  }
}
