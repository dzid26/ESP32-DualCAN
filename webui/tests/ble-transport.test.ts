// Closed-loop BLE transport tests against a mocked `BleClient`.
//
// These run under the repo's plain `node --test` runner (no browser, no radio,
// no real timers). The mock is injected through the `BleClientPort` seam on
// `BleTransport`; production wiring is untouched.
import './helpers/test-globals.ts';
import test from 'node:test';
import assert from 'node:assert/strict';
import { BleTransport } from '../src/transport/ble.ts';

interface MockDevice {
  deviceId: string;
  name: string | null;
}

/** Build a scriptable stand-in for the static `BleClient` surface. */
function makeClient(opts: { device?: MockDevice } = {}) {
  const device: MockDevice = opts.device ?? { deviceId: 'dev-1', name: 'Dorky-AAAA' };
  const state = {
    devices: [{ ...device }] as MockDevice[],
    initializeCalls: 0,
    connectCalls: [] as string[],
    disconnectCalls: [] as string[],
    requestDeviceCalls: [] as Array<Record<string, unknown>>,
    /** Last callback handed to connect() — used to fake a plugin disconnect. */
    onDisconnect: null as ((deviceId: string) => void) | null,
    /** Last callback handed to startNotifications() — used to fake a device notification. */
    onNotification: null as ((value: DataView) => void) | null,
  };
  const client = {
    async initialize() { state.initializeCalls++; },
    async requestDevice(options: Record<string, unknown>) {
      state.requestDeviceCalls.push(options);
      return { ...device };
    },
    async getDevices(ids?: string[]) {
      return state.devices.filter((d) => !ids?.length || ids.includes(d.deviceId));
    },
    async connect(deviceId: string, onDisconnect: (id: string) => void) {
      state.connectCalls.push(deviceId);
      state.onDisconnect = onDisconnect;
    },
    async disconnect(deviceId: string) { state.disconnectCalls.push(deviceId); },
    async writeWithoutResponse() {},
    async startNotifications(_deviceId: string, _service: string, _char: string, cb: (value: DataView) => void) {
      state.onNotification = cb;
    },
    async stopNotifications() {},
  };
  return { client, state };
}

test('reconnect succeeds after an unexpected disconnect without a chooser', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  localStorage.clear();

  // A real wall-clock gap forces the drop to be classified 'unexpected'
  // rather than the <1.5 s 'auth_fail' bucket.
  let now = 1_000_000;
  t.mock.method(Date, 'now', () => now);

  const { client, state } = makeClient();
  const transport = new BleTransport(client);

  const kinds: string[] = [];
  transport.onDisconnect((k) => kinds.push(k));

  await transport.connect();
  assert.equal(state.requestDeviceCalls.length, 1, 'first connect uses the picker');
  assert.equal(transport.connected, true);
  assert.equal(transport.savedDeviceId, 'dev-1');
  assert.equal(transport.savedDeviceName, 'Dorky-AAAA');

  now += 10_000;
  state.onDisconnect?.('dev-1');
  assert.deepEqual(kinds, ['unexpected']);
  assert.equal(transport.connected, false);

  // Silent reconnect: reuse the persisted device, never open the chooser.
  await transport.reconnect({ allowPicker: false });
  assert.equal(transport.connected, true);
  assert.equal(state.requestDeviceCalls.length, 1, 'reconnect must not open the chooser');
  assert.deepEqual(state.connectCalls, ['dev-1', 'dev-1']);
});

test('an immediate reconnect after a drop is single-shot and surfaces the failure', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  localStorage.clear();

  // A real wall-clock gap forces the drop to be classified 'unexpected'
  // rather than the <1.5 s 'auth_fail' bucket.
  let now = 1_000_000;
  t.mock.method(Date, 'now', () => now);

  const { client, state } = makeClient();
  const transport = new BleTransport(client);
  const kinds: string[] = [];
  transport.onDisconnect((k) => kinds.push(k));
  await transport.connect();

  now += 10_000;
  state.onDisconnect?.('dev-1');
  assert.deepEqual(kinds, ['unexpected']);

  // The peripheral still holds its previous link, so an immediate GATT
  // connect fails fast — as the real plugin does when the peer is not ready.
  let attempts = 0;
  client.connect = async (deviceId: string, onDisconnect: (id: string) => void) => {
    attempts++;
    if (attempts === 1) throw new Error('GATT error: connection failed');
    state.connectCalls.push(deviceId);
    state.onDisconnect = onDisconnect;
  };

  // `reconnect()` adds NO delay of its own: exactly one attempt is made and
  // its rejection propagates to the caller (the store is what waits + retries).
  await assert.rejects(transport.reconnect({ allowPicker: false }), /connection failed/);
  assert.equal(attempts, 1, 'a failed immediate reconnect is not retried inside the transport');
  assert.equal(transport.connected, false, 'the link stays down after the failed attempt');
  assert.equal(state.requestDeviceCalls.length, 1, 'allowPicker:false never opens the chooser');

  // Once the peer has had time to re-advertise, the same call succeeds.
  await transport.reconnect({ allowPicker: false });
  assert.equal(attempts, 2);
  assert.equal(transport.connected, true);
  assert.equal(state.requestDeviceCalls.length, 1, 'still no chooser');
});

test('persists deviceId/name in dc-ble-device and reuses them across instances', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  localStorage.clear();

  const { client, state } = makeClient({ device: { deviceId: 'dev-42', name: 'Dorky-BEEF' } });
  const first = new BleTransport(client);
  await first.connect();

  assert.deepEqual(
    JSON.parse(localStorage.getItem('dc-ble-device') ?? 'null'),
    { deviceId: 'dev-42', name: 'Dorky-BEEF' },
  );

  await first.disconnect();
  // The live session is gone, but the durable record survives the disconnect.
  assert.equal(first.connected, false);
  assert.equal(first.savedDeviceId, 'dev-42');
  assert.equal(first.savedDeviceName, 'Dorky-BEEF');
  assert.deepEqual(
    JSON.parse(localStorage.getItem('dc-ble-device') ?? 'null'),
    { deviceId: 'dev-42', name: 'Dorky-BEEF' },
  );

  // A fresh transport (as after a reload) restores from localStorage and
  // reconnects the saved device with no chooser.
  const { client: client2, state: state2 } = makeClient({ device: { deviceId: 'other', name: 'Other' } });
  state2.devices = [{ deviceId: 'dev-42', name: 'Dorky-BEEF' }];
  const second = new BleTransport(client2);
  assert.equal(second.savedDeviceId, 'dev-42');
  assert.equal(second.savedDeviceName, 'Dorky-BEEF');

  await second.connect();
  assert.equal(state2.requestDeviceCalls.length, 0, 'saved device is reused without the chooser');
  assert.deepEqual(state2.connectCalls, ['dev-42']);
});

test('classifies user-initiated teardown apart from fast auth failures', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  localStorage.clear();

  // user disconnect
  const { client, state } = makeClient();
  const transport = new BleTransport(client);
  const kinds: string[] = [];
  transport.onDisconnect((k) => kinds.push(k));
  await transport.connect();
  await transport.disconnect();
  state.onDisconnect?.('dev-1');
  assert.deepEqual(kinds, ['user']);

  // immediate drop (< AUTH_FAIL_THRESHOLD_MS) is treated as an auth failure
  const { client: client2, state: state2 } = makeClient({ device: { deviceId: 'dev-9', name: 'Dorky-9999' } });
  const transport2 = new BleTransport(client2);
  const kinds2: string[] = [];
  transport2.onDisconnect((k) => kinds2.push(k));
  await transport2.connect();
  state2.onDisconnect?.('dev-9');
  assert.deepEqual(kinds2, ['auth_fail']);
});

test('honours the firmware replacement signal and ignores a repeated disconnect', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  localStorage.clear();

  const { client, state } = makeClient();
  const transport = new BleTransport(client);
  const kinds: string[] = [];
  transport.onDisconnect((k) => kinds.push(k));
  await transport.connect();

  // Firmware sends [0xFD, 0x01] immediately before dropping us for another client.
  state.onNotification?.(new DataView(new Uint8Array([0xFD, 0x01]).buffer));
  state.onDisconnect?.('dev-1');
  assert.deepEqual(kinds, ['replaced'], 'firmware reason must override the timing heuristic');

  // The plugin sometimes fires the same disconnect twice; the second is ignored.
  state.onDisconnect?.('dev-1');
  assert.deepEqual(kinds, ['replaced']);
});

test('falls back to the picker when the saved device can no longer be reconnected', async (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  localStorage.clear();
  // A device saved by a previous session, now forgotten/unpaired by the OS.
  localStorage.setItem('dc-ble-device', JSON.stringify({ deviceId: 'gone-1', name: 'Dorky-GONE' }));

  const { client, state } = makeClient({ device: { deviceId: 'fresh-1', name: 'Dorky-NEW' } });
  client.connect = async (deviceId: string, onDisconnect: (id: string) => void) => {
    if (deviceId === 'gone-1') throw new Error('device no longer permitted');
    state.connectCalls.push(deviceId);
    state.onDisconnect = onDisconnect;
  };

  const transport = new BleTransport(client);
  await transport.connect();

  assert.deepEqual(state.connectCalls, ['fresh-1'], 'picker is used only after the saved device fails');
  assert.equal(state.requestDeviceCalls.length, 1, 'a failed silent reconnect must surface the chooser');
  assert.equal(transport.connected, true);
  assert.equal(transport.savedDeviceId, 'fresh-1', 'the newly-picked device replaces the stale one');
});
