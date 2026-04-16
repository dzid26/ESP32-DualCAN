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
  private _connected = false;

  get connected(): boolean {
    return this._connected;
  }

  async connect(): Promise<void> {
    if (!navigator.bluetooth) {
      throw new Error(
        'Web Bluetooth not available. Use Chrome/Edge on localhost or HTTPS.'
      );
    }

    this.device = await navigator.bluetooth.requestDevice({
      filters: [{ services: [SERVICE_UUID] }],
      optionalServices: [SERVICE_UUID],
    });

    this.device.addEventListener('gattserverdisconnected', () => {
      this._connected = false;
      console.log('BLE disconnected');
    });

    this.server = await this.device.gatt!.connect();
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

    this._connected = true;
    console.log('BLE connected to', this.device.name);
  }

  async disconnect(): Promise<void> {
    if (this.server?.connected) {
      this.server.disconnect();
    }
    this._connected = false;
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
