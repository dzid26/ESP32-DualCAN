// Composition root: the only module that wires the real BLE client.
import { BleClient } from '@capacitor-community/bluetooth-le';
import { Capacitor } from '@capacitor/core';
import { BleTransport } from '../transport/ble';
import { AppState } from './store.svelte';

export const app = new AppState({
  ble: new BleTransport(BleClient, () => Capacitor.getPlatform() === 'web'),
});
