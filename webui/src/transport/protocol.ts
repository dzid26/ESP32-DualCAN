import type { Transport } from './types';

/**
 * JSON-over-BLE protocol client.
 *
 * Wire format: 4-byte little-endian header + payload. The top nibble of
 * the header is a frame-type discriminator (0 = JSON, 1 = binary OTA
 * write); the bottom 28 bits are the payload length. Existing JSON
 * frames remain bit-compatible with the old format (top nibble = 0).
 * BLE fragments may split a frame; the receiver buffers until complete.
 */

/* Must mirror frame_buf.h on the firmware side. */
const FRAME_TYPE_JSON = 0x0;
const FRAME_TYPE_OTA_WRITE = 0x1;

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
  private busActivityCallback: ((bus: number) => void) | null = null;

  /** Register a callback for Berry print() log lines pushed from the device. */
  onLog(cb: (msg: string) => void) { this.logCallback = cb; }

  /** Register a callback for trace frames pushed by the device. Pass null to clear. */
  onTrace(cb: ((f: TraceFrame) => void) | null) { this.traceCallback = cb; }

  /** Register a callback for subscribed signal updates. Pass null to clear. */
  onSignal(cb: ((s: SignalUpdate) => void) | null) { this.signalCallback = cb; }

  /** Register a persistent callback fired for every push frame that carries a bus field.
   *  Fires for both trace and signal frames, independent of which view is active. */
  onBusActivity(cb: ((bus: number) => void) | null) { this.busActivityCallback = cb; }

  // Per-write chunk size for the underlying transport. The firmware
  // requests an ATT_MTU of 512 and Chrome/desktop hosts typically settle
  // on 247–517; 180 stays under MTU-3 on every stack we've tested
  // (including older Android builds that cap at 185) while leaving the
  // old 100-byte ceiling well behind — that bound was overly cautious
  // and was the main rate-limit on OTA chunk throughput.
  private readonly CHUNK = 180;

  private txMutex = Promise.resolve();

  constructor(transport: Transport) {
    this.transport = transport;
    transport.onReceive((data) => this.onRx(data));
  }

  /** Build a 4-byte little-endian length+type header. */
  private buildHeader(payloadLen: number, type: number): Uint8Array {
    if ((payloadLen & 0x0fffffff) !== payloadLen) {
      throw new Error(`payload too large for frame header: ${payloadLen}`);
    }
    const hdr = new Uint8Array(4);
    hdr[0] = payloadLen & 0xff;
    hdr[1] = (payloadLen >>> 8) & 0xff;
    hdr[2] = (payloadLen >>> 16) & 0xff;
    hdr[3] = ((payloadLen >>> 24) & 0x0f) | ((type & 0xf) << 4);
    return hdr;
  }

  /** Send a fully-built frame (header + payload) on the wire, serialised
   * through txMutex. The pending-promise must already be registered by
   * the caller and reachable via the response's id field. */
  private dispatchFrame(id: number, frame: Uint8Array): void {
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
  }

  async call<T = any>(op: string, params: Record<string, unknown> = {}, timeoutMs = 5000): Promise<T> {
    if (!this.transport.connected) throw new Error('Not connected');

    const id = this.nextId++;
    const reqBytes = this.encoder.encode(JSON.stringify({ op, id, ...params }));
    const hdr = this.buildHeader(reqBytes.length, FRAME_TYPE_JSON);
    const frame = new Uint8Array(4 + reqBytes.length);
    frame.set(hdr, 0);
    frame.set(reqBytes, 4);

    const promise = new Promise<T>((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      setTimeout(() => {
        if (this.pending.delete(id)) {
          reject(new Error(`Timeout waiting for response to ${op}`));
        }
      }, timeoutMs);
    });

    this.dispatchFrame(id, frame);
    return promise;
  }

  /** Send a raw firmware chunk in a binary frame.
   * Body = u32 LE id + raw bytes. Response is normal JSON {ok,id}. */
  private otaWriteBinary(bytes: Uint8Array, timeoutMs = 15_000): Promise<void> {
    if (!this.transport.connected) return Promise.reject(new Error('Not connected'));

    const id = this.nextId++;
    const bodyLen = 4 + bytes.length;
    const hdr = this.buildHeader(bodyLen, FRAME_TYPE_OTA_WRITE);
    const frame = new Uint8Array(4 + bodyLen);
    frame.set(hdr, 0);
    frame[4] = id & 0xff;
    frame[5] = (id >>> 8) & 0xff;
    frame[6] = (id >>> 16) & 0xff;
    frame[7] = (id >>> 24) & 0xff;
    frame.set(bytes, 8);

    const promise = new Promise<void>((resolve, reject) => {
      this.pending.set(id, { resolve: () => resolve(), reject });
      setTimeout(() => {
        if (this.pending.delete(id)) {
          reject(new Error('Timeout waiting for response to ota.write (binary)'));
        }
      }, timeoutMs);
    });

    this.dispatchFrame(id, frame);
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
          } else if (resp.type === 'trace') {
            if (this.busActivityCallback) this.busActivityCallback(resp.bus ?? 0);
            if (this.traceCallback) {
              const data = new Uint8Array(((resp.data ?? '') as string).match(/.{2}/g)?.map((h: string) => parseInt(h, 16)) ?? []);
              this.traceCallback({ bus: resp.bus ?? 0, id: resp.id ?? 0, ts: resp.ts ?? 0, data });
            }
          } else if (resp.type === 'signal') {
            if (this.busActivityCallback) this.busActivityCallback(resp.bus ?? 0);
            if (this.signalCallback) this.signalCallback({
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

  /** Write a named secret to NVS on the device. Write-only — never read back over BLE. */
  setSecret(name: string, value: string): Promise<void> {
    return this.call('settings.set_secret', { name, value });
  }

  /** Read a named secret from NVS. Returns null if not set. */
  getSecret(name: string): Promise<{ value: string | null }> {
    return this.call('settings.get_secret', { name });
  }

  /** Returns whether a named secret is currently stored on the device. */
  hasSecret(name: string): Promise<{ set: boolean }> {
    return this.call('settings.has_secret', { name });
  }

  /** Remove a named secret from NVS. No-op if it wasn't set. */
  clearSecret(name: string): Promise<void> {
    return this.call('settings.clear_secret', { name });
  }

  // ---- OTA (Over-the-Air firmware update) ----

  /** Begin an OTA session. Returns the max firmware size (bytes) of the
   * target partition. The device erases flash — this can take a few seconds. */
  otaBegin(): Promise<{ max_size: number }> {
    return this.call('ota.begin', {}, 30_000);
  }

  /** Finalise and validate the written firmware image. If `reboot` is true
   * (default), the device restarts into the new firmware.
   * If `size` is provided, the device verifies the byte count matches what
   * was actually written before flipping the boot partition. */
  otaEnd(reboot = true, size?: number): Promise<void> {
    const params: Record<string, unknown> = { reboot };
    if (size !== undefined) params.size = size;
    return this.call('ota.end', params, 15_000);
  }

  /** Abort an in-progress OTA session and free firmware-side resources. */
  otaAbort(): Promise<void> {
    return this.call('ota.abort');
  }

  /**
   * Stream a firmware binary through ota.begin + ota.write. Does NOT call
   * ota.end — the caller must follow up with otaEnd() (to commit) or
   * otaAbort() (to discard). Splitting finalisation lets the caller decide
   * the reboot flag *after* the upload finishes.
   *
   * `bin` — the raw firmware bytes.
   * `onProgress(sent, total)` — invoked after each acknowledged chunk.
   *
   * Throws on any transport / device error. The caller is responsible for
   * catching and calling otaAbort() to free firmware-side resources.
   */
  async streamFirmware(
    bin: Uint8Array,
    onProgress?: (sent: number, total: number) => void,
  ): Promise<void> {
    const { max_size } = await this.otaBegin();
    if (bin.length > max_size) {
      await this.otaAbort();
      throw new Error(`Firmware too large (${bin.length} bytes, max ${max_size})`);
    }

    // Send raw bytes in a binary frame — no base64 inflation, no JSON
    // parse on the firmware side. 4 KB stays under the device's 5 KB
    // OTA scratch + 16 KB RX buffer with room for the 8-byte header.
    const CHUNK = 4096;
    let sent = 0;
    while (sent < bin.length) {
      const end = Math.min(sent + CHUNK, bin.length);
      await this.otaWriteBinary(bin.subarray(sent, end));
      sent = end;
      onProgress?.(sent, bin.length);
    }
  }
}
