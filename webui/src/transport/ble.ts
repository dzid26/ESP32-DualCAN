import type { Transport } from './types';
import { BleClient, type BleDevice, type RequestBleDeviceOptions } from '@capacitor-community/bluetooth-le';
import { Capacitor } from '@capacitor/core';

// Nordic UART Service UUIDs (must match firmware ble_transport.c)
const SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const RX_CHAR_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // write to device
const TX_CHAR_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // notify from device

/** Firmware advertises as "Dorky-XXXX"; used as the chooser filter fallback. */
const DEVICE_NAME_PREFIX = 'Dorky';

/** Namespaced localStorage key for the last successfully-connected device. */
const STORAGE_KEY = 'dc-ble-device';

/** How the link came down, as inferred from timing + intent. */
export type DisconnectKind =
  | 'user'           // disconnect() was called explicitly
  | 'auth_fail'      // came down within AUTH_FAIL_THRESHOLD_MS — almost always stale OS bond / 517
  | 'replaced'       // another authorized client took over (firmware notified us)
  | 'unexpected';    // anything else (out of range, peer powered off, kicked)

/** Disconnects faster than this are treated as authentication failures. */
const AUTH_FAIL_THRESHOLD_MS = 1500;

interface PersistedDevice {
  deviceId: string;
  name: string | null;
}

/** True when running in a browser (as opposed to the Capacitor native shell). */
function isWebPlatform(): boolean {
  try {
    return Capacitor.getPlatform() === 'web';
  } catch {
    return true;
  }
}

export class BleTransport implements Transport {
  private deviceId: string | null = null;
  private deviceName_: string | null = null;
  /** Last successfully-connected device, durable across disconnects + reloads.
   *  Kept separate from deviceId so a dropped link can still be re-established
   *  without reshowing the system/browser chooser. */
  private lastDeviceId: string | null = null;
  private lastDeviceName: string | null = null;
  private receiveCb: ((data: Uint8Array) => void) | null = null;
  private changeCbs: Array<(connected: boolean) => void> = [];
  private disconnectCbs: Array<(kind: DisconnectKind) => void> = [];
  private _connected = false;
  private connectedAt = 0;
  private userInitiatedDisconnect = false;
  private initialized = false;
  /** Set to true while reconnect() is running so stale disconnect events
   *  from the plugin don't corrupt the state we're about to replace. */
  private reconnecting = false;
  /** Guards against the plugin firing onDisconnected twice for one event. */
  private disconnectHandled = false;
  /** Set by onNotification when the firmware signals the reason for the
   *  upcoming disconnect via a [0xFD, reason] notification. */
  private pendingDisconnectReason: DisconnectKind | null = null;

  constructor() {
    const saved = BleTransport.loadPersistedDevice();
    this.lastDeviceId = saved?.deviceId ?? null;
    this.lastDeviceName = saved?.name ?? null;
  }

  private static loadPersistedDevice(): PersistedDevice | null {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (!raw) return null;
      const parsed = JSON.parse(raw) as { deviceId?: unknown; name?: unknown };
      if (parsed && typeof parsed.deviceId === 'string' && parsed.deviceId) {
        return { deviceId: parsed.deviceId, name: typeof parsed.name === 'string' ? parsed.name : null };
      }
    } catch { /* ignore (private mode / SSR) */ }
    return null;
  }

  /** Remember the device so future connects can skip the chooser. */
  private rememberDevice(id: string, name: string | null): void {
    this.lastDeviceId = id;
    this.lastDeviceName = name;
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify({ deviceId: id, name }));
    } catch { /* ignore (private mode / quota) */ }
  }

  /** Last known device id, or null if none has been saved. */
  get savedDeviceId(): string | null { return this.lastDeviceId; }

  /** Advertised name of the last known device, or null. */
  get savedDeviceName(): string | null { return this.lastDeviceName; }

  private readonly onNotification = (value: DataView): void => {
    const data = new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
    // Firmware disconnect-reason notification: [0xFD, reason]
    if (data.length >= 2 && data[0] === 0xFD) {
      if (data[1] === 0x01) this.pendingDisconnectReason = 'replaced';
      return;
    }
    if (this.receiveCb) this.receiveCb(data);
  };

  private readonly onDisconnected = (_deviceId: string): void => {
    // The plugin sometimes fires this twice for one disconnect.
    if (this.disconnectHandled) return;
    this.disconnectHandled = true;

    // During reconnect we intentionally disconnect and then re-connect.
    // Ignore the disconnect event from the teardown — the subsequent
    // connect() will establish the new session.
    if (this.reconnecting) return;

    let kind: DisconnectKind;
    const sinceConnect = this.connectedAt ? Date.now() - this.connectedAt : Infinity;

    if (this.pendingDisconnectReason) {
      kind = this.pendingDisconnectReason;
      this.pendingDisconnectReason = null;
    } else if (this.userInitiatedDisconnect) {
      kind = 'user';
    } else if (sinceConnect < AUTH_FAIL_THRESHOLD_MS) {
      kind = 'auth_fail';
    } else {
      kind = 'unexpected';
    }
    this.userInitiatedDisconnect = false;
    this.connectedAt = 0;
    // Only the live session is cleared — lastDeviceId/Name persist so the
    // next connect can skip the chooser.
    this.deviceId = null;
    this.deviceName_ = null;
    this.setConnected(false);
    console.log(`BLE disconnected (${kind}, after ${sinceConnect}ms)`);
    this.disconnectCbs.forEach(cb => cb(kind));
  };

  get connected(): boolean {
    return this._connected;
  }

  /** Name of the currently-connected device (e.g. "Dorky-A3F1"), or null when disconnected. */
  get deviceName(): string | null {
    return this._connected ? this.deviceName_ : null;
  }

  private setConnected(val: boolean) {
    if (this._connected === val) return;
    this._connected = val;
    this.changeCbs.forEach(cb => cb(val));
  }

  /** Subscribe to connection state changes (fires on connect, disconnect, and unexpected drop). */
  onConnectionChange(cb: (connected: boolean) => void): void {
    this.changeCbs.push(cb);
  }

  /** Subscribe to disconnect events with an inferred classification. */
  onDisconnect(cb: (kind: DisconnectKind) => void): void {
    this.disconnectCbs.push(cb);
  }

  private async ensureInitialized(): Promise<void> {
    if (!this.initialized) {
      await BleClient.initialize();
      this.initialized = true;
    }
  }

  private async subscribeNotifications(): Promise<void> {
    if (!this.deviceId) throw new Error('Not connected');
    // stop + start to replace any stale callback from a previous session
    try { await BleClient.stopNotifications(this.deviceId, SERVICE_UUID, TX_CHAR_UUID); } catch { /* ignore */ }
    await BleClient.startNotifications(this.deviceId, SERVICE_UUID, TX_CHAR_UUID, this.onNotification);
  }

  /** Web Bluetooth's getDevices() is not implemented in every browser and the
   *  Capacitor plugin does not guard it on web, so feature-detect before use. */
  private canGetDevices(): boolean {
    if (!isWebPlatform()) return true;
    if (typeof navigator === 'undefined') return false;
    const bt = (navigator as unknown as { bluetooth?: { getDevices?: unknown } }).bluetooth;
    return !!bt && typeof bt.getDevices === 'function';
  }

  /** Resolve a previously-permitted device handle without showing a chooser. */
  private async getSavedDevice(id: string): Promise<BleDevice | null> {
    if (!this.canGetDevices()) return null;
    try {
      const devices = await BleClient.getDevices([id]);
      return devices.find(d => d.deviceId === id) ?? devices[0] ?? null;
    } catch (e) {
      console.log('BLE getDevices failed:', e);
      return null;
    }
  }

  /** Clear only the live session; the saved device id/name are preserved. */
  private async teardownAfterFailedConnect(): Promise<void> {
    const id = this.deviceId;
    this.deviceId = null;
    this.deviceName_ = null;
    this.connectedAt = 0;
    this.setConnected(false);
    if (id) { try { await BleClient.disconnect(id); } catch { /* ignore */ } }
  }

  /** Establish GATT + notifications for a known device, then mark connected. */
  private async establishSession(id: string, name: string | null): Promise<void> {
    this.deviceId = id;
    this.deviceName_ = name;
    this.disconnectHandled = false;
    this.userInitiatedDisconnect = false;
    try {
      await BleClient.connect(id, this.onDisconnected);
      this.connectedAt = Date.now();

      // Race startNotifications against a timeout — it can hang on Windows.
      await Promise.race([
        this.subscribeNotifications().then(() => console.log('BLE notifications started')),
        new Promise(r => setTimeout(r, 5000)).then(() => console.warn('BLE startNotifications timed out')),
      ]).catch(() => console.warn('BLE startNotifications failed — reconnect may help'));

      // If the connection dropped during notification setup (e.g. auth
      // failure), onDisconnected cleared deviceId — don't override.
      if (this.deviceId !== id) return;

      this.setConnected(true);
      this.rememberDevice(id, name);
      console.log('BLE connected to', name);
    } catch (e) {
      await this.teardownAfterFailedConnect();
      throw e;
    }
  }

  private async connectViaPicker(useNamePrefix: boolean): Promise<void> {
    const options: RequestBleDeviceOptions = useNamePrefix
      ? { namePrefix: DEVICE_NAME_PREFIX, optionalServices: [SERVICE_UUID] }
      : { services: [SERVICE_UUID], optionalServices: [SERVICE_UUID] };
    const device = await BleClient.requestDevice(options);
    await this.establishSession(device.deviceId, device.name ?? null);
  }

  async connect(): Promise<void> {
    await this.ensureInitialized();

    // Prefer a picker-less reconnect to the last known device.
    if (this.lastDeviceId) {
      try {
        await this.reconnect({ allowPicker: false });
        return;
      } catch (e) {
        console.log('BLE silent reconnect unavailable — showing device picker:', e);
      }
    }

    try {
      await this.connectViaPicker(false);
    } catch (e) {
      await this.teardownAfterFailedConnect();
      throw e;
    }
  }

  async disconnect(): Promise<void> {
    this.userInitiatedDisconnect = true;
    if (this.deviceId) {
      await BleClient.disconnect(this.deviceId);
    }
    this.deviceId = null;
    this.deviceName_ = null;
    this.connectedAt = 0;
    this.setConnected(false);
  }

  async send(data: Uint8Array): Promise<void> {
    if (!this.deviceId) throw new Error('Not connected');
    const dv = new DataView(data.buffer, data.byteOffset, data.byteLength);
    await BleClient.writeWithoutResponse(this.deviceId, SERVICE_UUID, RX_CHAR_UUID, dv);
  }

  /** Re-subscribe to TX characteristic notifications.
   *  Call when the notification stream appears stalled (no incoming data
   *  despite the GATT connection being up). */
  async restartNotifications(): Promise<void> {
    await this.subscribeNotifications();
  }

  /** Disconnect and reconnect to the last device without showing the
   *  Bluetooth chooser. Useful for recovering a wedged transport and for
   *  coming back after an OTA reboot.
   *
   *  @param opts.allowPicker when false (default true), never fall back to
   *  the system/browser chooser — required for gesture-less auto-reconnect. */
  async reconnect(opts: { allowPicker?: boolean } = {}): Promise<void> {
    const allowPicker = opts.allowPicker !== false;
    const id = this.lastDeviceId;

    this.reconnecting = true;
    try {
      // Tear down the current session in the plugin — onDisconnected will
      // see reconnecting=true and skip to avoid corrupting state.
      if (this.deviceId || this._connected) {
        const current = this.deviceId ?? id;
        this.userInitiatedDisconnect = true;
        if (current) { try { await BleClient.disconnect(current); } catch { /* ignore */ } }
        this.connectedAt = 0;
        this.setConnected(false);
      }

      // New session — allow disconnect events to be processed normally.
      this.reconnecting = false;
      this.disconnectHandled = false;

      if (!id) {
        if (!allowPicker) throw new Error('No device to reconnect to');
        await this.connectViaPicker(true);
        return;
      }

      // Silent path: getDevices() returns devices this origin already has
      // permission for, so connect() needs no chooser.
      const saved = await this.getSavedDevice(id);
      const targetId = saved?.deviceId ?? id;
      const targetName = saved?.name ?? this.lastDeviceName;
      try {
        await this.establishSession(targetId, targetName);
        return;
      } catch (e) {
        if (!allowPicker) throw e;
        console.log('BLE reconnect failed — showing device picker:', e);
      }

      await this.connectViaPicker(true);
    } catch (e) {
      await this.teardownAfterFailedConnect();
      throw e;
    } finally {
      this.reconnecting = false;
    }
  }

  onReceive(cb: (data: Uint8Array) => void): void {
    this.receiveCb = cb;
  }
}
