import type { Transport } from './types';
import { BleClient } from '@capacitor-community/bluetooth-le';

// Nordic UART Service UUIDs (must match firmware ble_transport.c)
const SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const RX_CHAR_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // write to device
const TX_CHAR_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // notify from device

/** How the link came down, as inferred from timing + intent. */
export type DisconnectKind =
  | 'user'           // disconnect() was called explicitly
  | 'auth_fail'      // came down within AUTH_FAIL_THRESHOLD_MS — almost always stale OS bond / 517
  | 'replaced'       // another authorized client took over (firmware notified us)
  | 'unexpected';    // anything else (out of range, peer powered off, kicked)

/** Disconnects faster than this are treated as authentication failures. */
const AUTH_FAIL_THRESHOLD_MS = 1500;

export class BleTransport implements Transport {
  private deviceId: string | null = null;
  private deviceName_: string | null = null;
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

  async connect(): Promise<void> {
    await this.ensureInitialized();

    try {
      const device = await BleClient.requestDevice({
        services: [SERVICE_UUID],
        optionalServices: [SERVICE_UUID],
      });

      this.deviceId = device.deviceId;
      this.deviceName_ = device.name ?? null;
      this.disconnectHandled = false;

      await BleClient.connect(device.deviceId, this.onDisconnected);
      this.connectedAt = Date.now();

      // Race startNotifications against a timeout — it can hang on Windows.
      await Promise.race([
        this.subscribeNotifications().then(() => console.log('BLE notifications started')),
        new Promise(r => setTimeout(r, 5000)).then(() => console.warn('BLE startNotifications timed out')),
      ]).catch(() => console.warn('BLE startNotifications failed — reconnect may help'));

      // If the connection dropped during notification setup (e.g. auth
      // failure), onDisconnected cleared deviceId — don't override.
      if (!this.deviceId) return;

      this.setConnected(true);
      console.log('BLE connected to', device.name);
    } catch (e) {
      // Null our state and tell the plugin to release any lingering
      // connection so the next attempt starts fresh.
      const id = this.deviceId;
      this.deviceId = null;
      this.deviceName_ = null;
      this.connectedAt = 0;
      this.setConnected(false);
      if (id) { try { await BleClient.disconnect(id); } catch { /* ignore */ } }
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

  /** Disconnect and reconnect to the same device without showing the
   *  Bluetooth picker. Useful for recovering a wedged transport. */
  async reconnect(): Promise<void> {
    const id = this.deviceId;
    if (!id) throw new Error('No device to reconnect to');

    this.reconnecting = true;
    this.userInitiatedDisconnect = true;

    try {
      // Tear down the current session in the plugin — onDisconnected will
      // see reconnecting=true and skip to avoid corrupting state.
      try { await BleClient.disconnect(id); } catch { /* ignore */ }
      this.connectedAt = 0;
      this.setConnected(false);

      // New session — allow disconnect events to be processed normally.
      this.reconnecting = false;
      this.disconnectHandled = false;

      await BleClient.connect(id, this.onDisconnected);
      this.deviceId = id;
      this.connectedAt = Date.now();

      await this.subscribeNotifications();

      // If the connection dropped during notification setup, deviceId
      // was already cleared by onDisconnected.
      if (!this.deviceId) return;

      this.setConnected(true);
      console.log('BLE reconnected');
    } catch (e) {
      // Null our state and tell the plugin to release any lingering
      // connection so the next attempt starts fresh.
      this.deviceId = null;
      this.deviceName_ = null;
      this.connectedAt = 0;
      this.setConnected(false);
      try { await BleClient.disconnect(id); } catch { /* ignore */ }
      throw e;
    }
  }

  onReceive(cb: (data: Uint8Array) => void): void {
    this.receiveCb = cb;
  }
}
