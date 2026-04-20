import type { Transport } from './types';

// Nordic UART Service UUIDs (must match firmware ble_transport.c)
const SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const RX_CHAR_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // write to device
const TX_CHAR_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // notify from device

export class BleTransport implements Transport {
  private device: BluetoothDevice | null = null;
  private server: BluetoothRemoteGATTServer | null = null;
  private rxChar: BluetoothRemoteGATTCharacteristic | null = null;
  private txChar: BluetoothRemoteGATTCharacteristic | null = null;
  private receiveCb: ((data: Uint8Array) => void) | null = null;
  private changeCbs: Array<(connected: boolean) => void> = [];
  private _connected = false;

  get connected(): boolean {
    return this._connected;
  }

  private setConnected(val: boolean) {
    this._connected = val;
    this.changeCbs.forEach(cb => cb(val));
  }

  /** Subscribe to connection state changes (fires on connect, disconnect, and unexpected drop). */
  onConnectionChange(cb: (connected: boolean) => void): void {
    this.changeCbs.push(cb);
  }

  private async setupDevice(device: BluetoothDevice): Promise<void> {
    device.addEventListener('gattserverdisconnected', () => {
      this.setConnected(false);
      console.log('BLE disconnected');
    });

    this.server = await device.gatt!.connect();
    const service = await this.server.getPrimaryService(SERVICE_UUID);

    this.rxChar = await service.getCharacteristic(RX_CHAR_UUID);
    this.txChar = await service.getCharacteristic(TX_CHAR_UUID);

    await this.txChar.startNotifications();
    this.txChar.addEventListener('characteristicvaluechanged', (event) => {
      const value = (event.target as BluetoothRemoteGATTCharacteristic).value;
      if (value && this.receiveCb) {
        this.receiveCb(new Uint8Array(value.buffer));
      }
    });

    this.device = device;
    this.setConnected(true);
    console.log('BLE connected to', device.name);
  }

  /** Try to silently reconnect to a previously authorized device (no user prompt).
   *  Returns true if reconnected, false otherwise. */
  async tryAutoConnect(): Promise<boolean> {
    if (!navigator.bluetooth) return false;
    const getDevices = (navigator.bluetooth as any).getDevices?.bind(navigator.bluetooth);
    if (!getDevices) return false;
    try {
      const devices: BluetoothDevice[] = await getDevices();
      for (const device of devices) {
        if (!device.gatt) continue;
        try {
          await this.setupDevice(device);
          return true;
        } catch {
          // device out of range or unavailable — try next
        }
      }
    } catch {
      // getDevices not permitted or unavailable
    }
    return false;
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
    if (this.server?.connected) {
      this.server.disconnect();
    }
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
