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

export type BusStatus = 'idle' | 'good' | 'tx_error' |'rx_error' |  'error';

export interface BusStatusUpdate {
  bus: number;
  status: BusStatus;
  /** RX frames-per-second over the firmware's last ~1 s window. */
  rate: number;
}

export interface ScriptUpdate {
  filename: string;
  enabled: boolean;
  errored: boolean;
  error?: string;
}

export interface CpuStatus {
  /** Total FreeRTOS CPU load percentage (0–100). */
  load: number;
}

type Pending = {
  resolve: (value: any) => void;
  reject: (err: Error) => void;
};

/** Runtime log-level keywords accepted by log.set_level. */
export type LogLevel = 'none' | 'error' | 'warn' | 'info' | 'debug' | 'verbose';

/** ESP_LOG output (ANSI-stripped "L (ts) tag: message" line). */
export interface RawLogPush   { raw: string }
/** Berry print() output (already structured). */
export interface BerryLogPush { level: string; src: string; msg: string }
export type LogPush = RawLogPush | BerryLogPush;

export class Protocol {
  private transport: Transport;
  private rxBuf = new Uint8Array(0);
  private pending = new Map<number, Pending>();
  private nextId = 1;
  private encoder = new TextEncoder();
  private decoder = new TextDecoder();
  private logCallback: ((entry: LogPush) => void) | null = null;
  private traceCallback: ((f: TraceFrame) => void) | null = null;
  private signalCallback: ((s: SignalUpdate) => void) | null = null;
  private busStatusCallback: ((u: BusStatusUpdate) => void) | null = null;
  private scriptUpdateCallback: ((u: ScriptUpdate) => void) | null = null;
  private cpuStatusCallback: ((u: CpuStatus) => void) | null = null;

  /** Register a callback for device log pushes (ESP_LOG and Berry print). */
  onLog(cb: (entry: LogPush) => void) { this.logCallback = cb; }

  /** Register a callback for trace frames pushed by the device. Pass null to clear. */
  onTrace(cb: ((f: TraceFrame) => void) | null) { this.traceCallback = cb; }

  /** Register a callback for subscribed signal updates. Pass null to clear. */
  onSignal(cb: ((s: SignalUpdate) => void) | null) { this.signalCallback = cb; }

  /** Register a callback for bus_status push frames from the device. */
  onBusStatus(cb: ((u: BusStatusUpdate) => void) | null) { this.busStatusCallback = cb; }

  /** Register a callback for script_update push frames from the device. */
  onScriptUpdate(cb: ((u: ScriptUpdate) => void) | null) { this.scriptUpdateCallback = cb; }

  /** Register a callback for cpu_status push frames from the device. */
  onCpuStatus(cb: ((u: CpuStatus) => void) | null) { this.cpuStatusCallback = cb; }

  // Per-write chunk size for the underlying transport. The firmware
  // requests an ATT_MTU of 512 and Chrome/desktop hosts typically settle
  // on 247–517; 180 stays under MTU-3 on every stack we've tested
  // (including older Android builds that cap at 185) while leaving the
  // old 100-byte ceiling well behind — that bound was overly cautious
  // and was the main rate-limit on OTA chunk throughput.
  /** Per-write chunk size for the underlying BLE transport.  Initialised to
   *  a safe fallback and bumped to ATT_MTU-3 once ble.mtu returns. */
  private CHUNK = 180;

  private txMutex = Promise.resolve();
  /** Timestamp of the last byte received via onRx. 0 if nothing received yet. */
  private lastRxTimestamp = 0;
  /** Fired once per stall episode when call() detects the notification stream
   *  is likely dead (no data received for > 10 s while connected). */
  private stallCb: (() => void) | null = null;
  /** Guards against firing stallCb repeatedly for the same episode. */
  private stallReported = false;

  constructor(transport: Transport) {
    this.transport = transport;
    transport.onReceive((data) => this.onRx(data));
  }

  /** Register a callback that fires when the notification stream appears
   *  stalled (no incoming data for > 10 s despite apparently being connected).
   *  Fires at most once per stall episode; resets when data arrives. */
  onStall(cb: () => void): void {
    this.stallCb = cb;
  }

  /** Query the negotiated ATT MTU from the device and adjust CHUNK to
   *  MTU-3.  Call once after connect, before sending large frames. */
  async adjustChunkFromMtu(): Promise<void> {
    try {
      const { mtu } = await this.call<{ mtu: number }>('ble.mtu');
      if (typeof mtu === 'number' && mtu > 200) {
        this.CHUNK = mtu - 3;
      }
    } catch {
      /* keep the 180 fallback */
    }
  }

  /** Clear all state after a disconnect. Rejects all pending requests. */
  reset(): void {
    this.rxBuf = new Uint8Array(0);
    this.lastRxTimestamp = 0;
    this.stallReported = false;
    for (const [id, p] of this.pending) {
      this.pending.delete(id);
      p.reject(new Error('Disconnected'));
    }
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
  private dispatchFrame(id: number, frame: Uint8Array): Promise<void> {
    let begin!: () => void;
    const started = new Promise<void>(r => begin = r);
    const doSend = async () => {
      begin();
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
    return started;
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
      this.dispatchFrame(id, frame).then(() => {
        if (!this.pending.has(id)) return;
        setTimeout(() => {
          if (this.pending.delete(id)) {
            reject(new Error(`Timeout waiting for response to ${op}`));
            /* If the firmware sent a partial frame before stalling, the rxBuf
             * contains stale bytes that poison all future parsing — the header
             * says "frame is N bytes" but the rest never arrives, so onRx
             * keeps blocking waiting for more data.  Reset to recover without
             * requiring a full disconnect. */
            this.rxBuf = new Uint8Array(0);
            if (!this.stallReported && this.stallCb &&
                this.transport.connected &&
                this.lastRxTimestamp > 0 &&
                Date.now() - this.lastRxTimestamp > 10_000) {
              this.stallReported = true;
              this.stallCb();
            }
          }
        }, timeoutMs);
      });
    });

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
      this.dispatchFrame(id, frame).then(() => {
        if (!this.pending.has(id)) return;
        setTimeout(() => {
          if (this.pending.delete(id)) {
            reject(new Error('Timeout waiting for response to ota.write (binary)'));
          }
        }, timeoutMs);
      });
    });

    return promise;
  }

  private onRx(data: Uint8Array) {
    this.lastRxTimestamp = Date.now();
    this.stallReported = false;       // data is flowing again
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
            this.logCallback(
              resp.raw != null
                ? { raw: resp.raw }
                : { level: resp.level ?? 'info', src: resp.src ?? 'berry', msg: resp.msg ?? '' }
            );
          } else if (resp.type === 'trace') {
            if (this.traceCallback) {
              const data = new Uint8Array(((resp.data ?? '') as string).match(/.{2}/g)?.map((h: string) => parseInt(h, 16)) ?? []);
              this.traceCallback({ bus: resp.bus ?? 0, id: resp.id ?? 0, ts: resp.ts ?? 0, data });
            }
          } else if (resp.type === 'signal') {
            if (this.signalCallback) this.signalCallback({
              name: resp.name ?? '',
              bus: resp.bus ?? 0,
              value: resp.value ?? 0,
              prev: resp.prev ?? 0,
            });
          } else if (resp.type === 'bus_status') {
            if (this.busStatusCallback) this.busStatusCallback({
              bus: resp.bus ?? 0,
              status: (resp.status ?? 'idle') as BusStatus,
              rate: resp.rate ?? 0,
            });
          } else if (resp.type === 'script_update') {
            if (this.scriptUpdateCallback) this.scriptUpdateCallback({
              filename: resp.script?.filename ?? '',
              enabled: !!resp.script?.enabled,
              errored: !!resp.script?.errored,
              error: resp.script?.error,
            });
          } else if (resp.type === 'cpu_status') {
            if (this.cpuStatusCallback) this.cpuStatusCallback({
              load: resp.load ?? 0,
            });
          }
          continue;
        }
        const p = this.pending.get(resp.id);
        if (!p) continue;
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

  systemInfo(): Promise<{ fw_version: string; proto_version: number; scripting_api_version?: number }> {
    return this.call('system.info');
  }

  systemReboot(): Promise<void> {
    return this.call('system.reboot');
  }

  systemFactoryReset(): Promise<void> {
    return this.call('system.factory_reset');
  }

  listScripts(): Promise<{ scripts: ScriptInfo[] }> {
    return this.call('script.list');
  }

  writeScript(filename: string, code: string, runtime?: string): Promise<void> {
    const params: Record<string, unknown> = { filename, code };
    if (runtime !== undefined) params.runtime = runtime;
    return this.call('script.write', params);
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

  getBusConfig(): Promise<{ buses: Array<{ bus: number; bitrate_kbps: number }> }> {
    return this.call('bus.get_config');
  }

  /** Set bitrate for a single bus. Change is applied immediately (hot reconfigure). */
  setBusBitrate(bus: number, bitrate_kbps: number): Promise<void> {
    return this.call('bus.set_bitrate', { bus, bitrate_kbps });
  }

  /** Toggle simulation mode (TX routed to log instead of bus). Optional bus filter. */
  setSimMode(enabled: boolean, bus?: number): Promise<void> {
    const params: Record<string, unknown> = { enabled };
    if (bus !== undefined) params.bus = bus;
    return this.call('sim.set', params);
  }

  /** Read the firmware's current runtime log level for tag "*". */
  getLogLevel(): Promise<{ level: LogLevel }> {
    return this.call('log.get_level');
  }

  /** Set runtime log level. Tag "*" (default) applies to every tag.
   * Levels above firmware's CONFIG_LOG_MAXIMUM_LEVEL are accepted but
   * have nothing to emit (those strings are compile-time stripped). */
  setLogLevel(level: LogLevel, tag = '*'): Promise<void> {
    return this.call('log.set_level', { level, tag });
  }

  bleStatus(): Promise<{ pairing_open: boolean; bond_count: number }> {
    return this.call('ble.status');
  }

  /** Open the device's pairing window for 60 s (same effect as pressing BOOT). */
  bleUnlockPairing(): Promise<void> {
    return this.call('ble.unlock_pairing');
  }

  /** Wipe all stored bonds on the device. Will disconnect the current client. */
  bleResetPairs(): Promise<void> {
    return this.call('ble.reset_pairs');
  }

  /** Tesla BLE keypair + VIN status. */
  teslaStatus(): Promise<{ has_key: boolean; public_key_hex?: string; vin?: string }> {
    return this.call('tesla.status');
  }

  /** Generate a fresh P-256 keypair and persist it. Returns the new status. */
  teslaGenKey(): Promise<{ has_key: boolean; public_key_hex?: string; vin?: string }> {
    return this.call('tesla.gen_key');
  }

  /** Wipe the Tesla keypair + VIN from NVS. */
  teslaReset(): Promise<void> {
    return this.call('tesla.reset');
  }

  /** Store the VIN (17 chars). Returns the new status. */
  teslaSetVin(vin: string): Promise<{ has_key: boolean; public_key_hex?: string; vin?: string }> {
    return this.call('tesla.set_vin', { vin });
  }

  /** Scan for nearby Tesla VCSEC advertisements. */
  teslaScan(duration_ms?: number): Promise<{ devices: Array<{ addr: string; name: string; rssi: number }> }> {
    return this.call('tesla.scan', duration_ms !== undefined ? { duration_ms } : undefined);
  }

  /** Connect to a Tesla and send the whitelist add-key message. */
  teslaPair(addr: string, addr_type?: number): Promise<void> {
    return this.call('tesla.pair', { addr, addr_type: addr_type ?? 0 });
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
