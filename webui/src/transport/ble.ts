import type { Transport } from './types';

// Nordic UART Service UUIDs (must match firmware ble_transport.c)
const SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const RX_CHAR_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // write to device
const TX_CHAR_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // notify from device

/** How the link came down, as inferred from timing + intent. Web Bluetooth
 * doesn't expose the HCI reason, so we make do with what we can observe. */
export type DisconnectKind =
  | 'user'           // disconnect() was called explicitly
  | 'auth_fail'      // came down within AUTH_FAIL_THRESHOLD_MS — almost always stale OS bond / 517
  | 'unexpected';    // anything else (out of range, peer powered off, kicked)

/** Disconnects faster than this are treated as authentication failures —
 * a healthy GATT setup takes at least a few hundred ms. */
const AUTH_FAIL_THRESHOLD_MS = 1500;

export class BleTransport implements Transport {
  private device: BluetoothDevice | null = null;
  private server: BluetoothRemoteGATTServer | null = null;
  private rxChar: BluetoothRemoteGATTCharacteristic | null = null;
  private txChar: BluetoothRemoteGATTCharacteristic | null = null;
  private receiveCb: ((data: Uint8Array) => void) | null = null;
  private changeCbs: Array<(connected: boolean) => void> = [];
  private disconnectCbs: Array<(kind: DisconnectKind) => void> = [];
  private _connected = false;
  private connectedAt = 0;
  private userInitiatedDisconnect = false;
  /** Guards against Windows firing gattserverdisconnected twice for one event. */
  private disconnectHandled = false;
  private readonly handleGattServerDisconnected = () => {
    if (this.disconnectHandled) return;
    this.disconnectHandled = true;

    const sinceConnect = Date.now() - this.connectedAt;
    let kind: DisconnectKind;
    if (this.userInitiatedDisconnect) kind = 'user';
    else if (sinceConnect < AUTH_FAIL_THRESHOLD_MS) kind = 'auth_fail';
    else kind = 'unexpected';
    this.userInitiatedDisconnect = false;
    this.connectedAt = 0;
    this.server = null;
    this.rxChar = null;
    this.txChar = null;
    this.device = null;
    this.setConnected(false);
    console.log(`BLE disconnected (${kind}, after ${sinceConnect}ms)`);
    this.disconnectCbs.forEach(cb => cb(kind));
  };
  private readonly handleCharacteristicValueChanged = (event: Event) => {
    const value = (event.target as BluetoothRemoteGATTCharacteristic).value;
    if (value && this.receiveCb) {
      this.receiveCb(new Uint8Array(value.buffer));
    }
  };

  get connected(): boolean {
    return this._connected;
  }

  /** Name of the currently-connected device (e.g. "Dorky-A3F1"), or null when disconnected. */
  get deviceName(): string | null {
    return this._connected ? (this.device?.name ?? null) : null;
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

  private async setupDevice(device: BluetoothDevice): Promise<void> {
    // Do all I/O first — any pending gattserverdisconnected from the previous
    // disconnect fires during these awaits with no listener attached → dropped.
    this.server = await device.gatt!.connect();
    const service = await this.server.getPrimaryService(SERVICE_UUID);

    this.rxChar = await service.getCharacteristic(RX_CHAR_UUID);
    this.txChar = await service.getCharacteristic(TX_CHAR_UUID);

    this.device = device;
    this.connectedAt = Date.now();
    this.disconnectHandled = false;

    // Attach the disconnect listener now — any stale events from the previous
    // disconnect have already fired during the I/O awaits above.
    device.removeEventListener('gattserverdisconnected', this.handleGattServerDisconnected);
    device.addEventListener('gattserverdisconnected', this.handleGattServerDisconnected);

    // Subscribe to incoming data. startNotifications can hang on Windows,
    // so race it against a short timeout — setupDevice always completes,
    // and if it resolves later, notifications kick in automatically.
    // Don't setConnected(true) until after this — on Android, concurrent
    // GATT ops (protocol writes from onConnChange) race with notifications.
    this.txChar.addEventListener('characteristicvaluechanged', this.handleCharacteristicValueChanged);
    await Promise.race([
      this.txChar.startNotifications().then(() => console.log('BLE notifications started')),
      new Promise(r => setTimeout(r, 5000)).then(() => console.warn('BLE startNotifications timed out')),
    ]).catch(() => console.warn('BLE startNotifications failed — reconnect may help'));

    this.setConnected(true);
    console.log('BLE connected to', device.name);
  }

  async connect(): Promise<void> {
    if (!navigator.bluetooth) {
      throw new Error(
        'Web Bluetooth not available. Use Chrome/Edge on localhost or HTTPS.'
      );
    }

    const device = await navigator.bluetooth.requestDevice({
      filters: [{ services: [SERVICE_UUID] }],
      optionalServices: [SERVICE_UUID],
    });

    await this.setupDevice(device);
  }

  async disconnect(): Promise<void> {
    this.userInitiatedDisconnect = true;
    if (this.server?.connected) {
      this.server.disconnect();
    }
    // On Windows, gattserverdisconnected can fire async. Remove the listener
    // so a stale event doesn't corrupt a subsequent connection.
    if (this.device) {
      this.device.removeEventListener('gattserverdisconnected', this.handleGattServerDisconnected);
    }
    this.server = null;
    this.rxChar = null;
    this.txChar = null;
    this.device = null;
    this.connectedAt = 0;
    this.setConnected(false);
  }

  async send(data: Uint8Array): Promise<void> {
    if (!this.rxChar) throw new Error('Not connected');
    // @ts-expect-error TS 5.x ArrayBufferLike vs ArrayBuffer mismatch — runtime is fine
    await this.rxChar.writeValueWithoutResponse(data);
  }

  onReceive(cb: (data: Uint8Array) => void): void {
    this.receiveCb = cb;
  }
}
