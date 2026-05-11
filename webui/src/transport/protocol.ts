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

export interface TraceFrame {
  bus: number;
  id: number;
  ts: number;        // ms since boot
  data: Uint8Array;
}

export interface SignalUpdate {
  name: string;
  bus: number;
  value: number;
  prev: number;
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
  private traceCallback: ((f: TraceFrame) => void) | null = null;
  private signalCallback: ((s: SignalUpdate) => void) | null = null;

  /** Register a callback for Berry print() log lines pushed from the device. */
  onLog(cb: (msg: string) => void) { this.logCallback = cb; }

  /** Register a callback for trace frames pushed by the device. Pass null to clear. */
  onTrace(cb: ((f: TraceFrame) => void) | null) { this.traceCallback = cb; }

  /** Register a callback for subscribed signal updates. Pass null to clear. */
  onSignal(cb: ((s: SignalUpdate) => void) | null) { this.signalCallback = cb; }

  // Conservative chunk size for BLE writes. Real MTU is usually higher
  // after negotiation, but Web Bluetooth doesn't expose it. 100 B works
  // on every stack we've tested.
  private readonly CHUNK = 100;

  private txMutex = Promise.resolve();

  constructor(transport: Transport) {
    this.transport = transport;
    transport.onReceive((data) => this.onRx(data));
  }

  async call<T = any>(op: string, params: Record<string, unknown> = {}, timeoutMs = 5000): Promise<T> {
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
      }, timeoutMs);
    });

    const doSend = async () => {
      try {
        for (let off = 0; off < frame.length; off += this.CHUNK) {
          const chunk = frame.subarray(off, Math.min(off + this.CHUNK, frame.length));
          await this.transport.send(chunk);
        }
      } catch (err) {
        const p = this.pending.get(id);
        if (p) {
          this.pending.delete(id);
          p.reject(err instanceof Error ? err : new Error(String(err)));
        }
      }
    };

    this.txMutex = this.txMutex.then(doSend);

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
          if (resp.type === 'log' && this.logCallback) {
            this.logCallback(resp.msg ?? '');
          } else if (resp.type === 'trace' && this.traceCallback) {
            const data = new Uint8Array(((resp.data ?? '') as string).match(/.{2}/g)?.map((h: string) => parseInt(h, 16)) ?? []);
            this.traceCallback({ bus: resp.bus ?? 0, id: resp.id ?? 0, ts: resp.ts ?? 0, data });
          } else if (resp.type === 'signal' && this.signalCallback) {
            this.signalCallback({
              name: resp.name ?? '',
              bus: resp.bus ?? 0,
              value: resp.value ?? 0,
              prev: resp.prev ?? 0,
            });
          }
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

  systemInfo(): Promise<{ fw_version: string; proto_version: number }> {
    return this.call('system.info');
  }

  systemReboot(): Promise<void> {
    return this.call('system.reboot');
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

  /** Subscribe to a signal — pushes arrive via onSignal() until unsubscribed. */
  subscribeSignal(name: string, bus = 0): Promise<void> {
    return this.call('signal.subscribe', { name, bus });
  }

  /** Unsubscribe a single signal, or pass no name to clear all subscriptions. */
  unsubscribeSignal(name?: string, bus = 0): Promise<void> {
    const params: Record<string, unknown> = { bus };
    if (name !== undefined) params.name = name;
    return this.call('signal.unsubscribe', params);
  }

  /** Start streaming raw frames. `buses` defaults to both. */
  traceStart(buses: number[] = [0, 1]): Promise<void> {
    return this.call('trace.start', { buses });
  }

  traceStop(): Promise<void> {
    return this.call('trace.stop');
  }

  /** Toggle simulation mode (TX routed to log instead of bus). Optional bus filter. */
  setSimMode(enabled: boolean, bus?: number): Promise<void> {
    const params: Record<string, unknown> = { enabled };
    if (bus !== undefined) params.bus = bus;
    return this.call('sim.set', params);
  }

  wifiStatus(): Promise<{ connected: boolean; ssid: string; ip: string }> {
    return this.call('wifi.status');
  }

  wifiSetCreds(ssid: string, psk: string): Promise<void> {
    return this.call('wifi.set_creds', { ssid, psk });
  }

  // ---- OTA (Over-the-Air firmware update) ----

  /** Begin an OTA session. Returns the max firmware size (bytes) of the
   * target partition. The device erases flash — this can take a few seconds. */
  otaBegin(): Promise<{ max_size: number }> {
    return this.call('ota.begin', {}, 30_000);
  }

  /** Send one base64-encoded chunk of firmware data. */
  otaWriteChunk(b64: string): Promise<void> {
    return this.call('ota.write', { data: b64 }, 15_000);
  }

  /** Finalise and validate the written firmware image. If `reboot` is true
   * (default), the device restarts into the new firmware. */
  otaEnd(reboot = true): Promise<void> {
    return this.call('ota.end', { reboot }, 15_000);
  }

  /** Abort an in-progress OTA session and free firmware-side resources. */
  otaAbort(): Promise<void> {
    return this.call('ota.abort');
  }

  /**
   * High-level: upload an entire firmware binary via BLE OTA.
   *
   * `bin` — the raw firmware ArrayBuffer / Uint8Array.
   * `onProgress(sent, total)` — called after each chunk is acknowledged.
   * `reboot` — restart into new firmware when done (default true).
   *
   * Throws on any error (caller should catch and show to user).
   */
  async uploadFirmware(
    bin: Uint8Array,
    onProgress?: (sent: number, total: number) => void,
  ): Promise<void> {
    const { max_size } = await this.otaBegin();
    if (bin.length > max_size) {
      await this.otaAbort();
      throw new Error(`Firmware too large (${bin.length} bytes, max ${max_size})`);
    }

    // Send in ~4 KB raw chunks → ~5.3 KB base64.  Keeps JSON frames well
    // under the 16 KB RX buffer on the firmware side.
    const CHUNK = 4096;
    let sent = 0;
    while (sent < bin.length) {
      const end = Math.min(sent + CHUNK, bin.length);
      const slice = bin.subarray(sent, end);
      const b64 = uint8ToBase64(slice);
      await this.otaWriteChunk(b64);
      sent = end;
      onProgress?.(sent, bin.length);
    }
  }
}

/** Encode Uint8Array to standard base64 string. */
function uint8ToBase64(bytes: Uint8Array): string {
  let binary = '';
  for (let i = 0; i < bytes.length; i++) {
    binary += String.fromCharCode(bytes[i]);
  }
  return btoa(binary);
}
