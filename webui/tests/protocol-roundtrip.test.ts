import test from 'node:test';
import assert from 'node:assert/strict';
import { Protocol } from '../src/transport/protocol.ts';
import type { Transport } from '../src/transport/types.ts';

interface RecordedReq {
  op: string;
  id: number;
  params: Record<string, unknown>;
}

class FakeDevice {
  rx = new Uint8Array(0);
  recorded: RecordedReq[] = [];
  handler: (req: RecordedReq) => unknown = () => null;

  ingest(data: Uint8Array, deliver: (frame: Uint8Array) => void): void {
    const merged = new Uint8Array(this.rx.length + data.length);
    merged.set(this.rx);
    merged.set(data, this.rx.length);
    this.rx = merged;

    while (this.rx.length >= 4) {
      const len = this.rx[0] | (this.rx[1] << 8) | (this.rx[2] << 16) | (this.rx[3] << 24);
      if (this.rx.length < 4 + len) break;

      const json = new TextDecoder().decode(this.rx.subarray(4, 4 + len));
      this.rx = this.rx.subarray(4 + len);

      const { op, id, ...params } = JSON.parse(json);
      const req: RecordedReq = { op, id, params };
      this.recorded.push(req);

      try {
        const result = this.handler(req);
        deliver(this.encode({ id, ok: true, result: result ?? null }));
      } catch (e) {
        const msg = e instanceof Error ? e.message : String(e);
        deliver(this.encode({ id, ok: false, error: msg }));
      }
    }
  }

  push(deliver: (frame: Uint8Array) => void, frame: object): void {
    deliver(this.encode(frame));
  }

  encode(obj: object): Uint8Array {
    const bytes = new TextEncoder().encode(JSON.stringify(obj));
    const out = new Uint8Array(4 + bytes.length);
    out[0] = bytes.length & 0xff;
    out[1] = (bytes.length >> 8) & 0xff;
    out[2] = (bytes.length >> 16) & 0xff;
    out[3] = (bytes.length >> 24) & 0xff;
    out.set(bytes, 4);
    return out;
  }
}

class FakeTransport implements Transport {
  connected = true;
  rxCb: ((d: Uint8Array) => void) | null = null;
  device: FakeDevice;

  constructor(device: FakeDevice) {
    this.device = device;
  }

  async connect(): Promise<void> {}

  async disconnect(): Promise<void> {
    this.connected = false;
  }

  async send(data: Uint8Array): Promise<void> {
    this.device.ingest(data, (frame) => this.rxCb?.(frame));
  }

  onReceive(cb: (d: Uint8Array) => void): void {
    this.rxCb = cb;
  }
}

function makePair(): { device: FakeDevice; transport: FakeTransport; proto: Protocol } {
  const device = new FakeDevice();
  const transport = new FakeTransport(device);
  const proto = new Protocol(transport);
  return { device, transport, proto };
}

function rx(transport: FakeTransport): (d: Uint8Array) => void {
  assert.ok(transport.rxCb);
  return transport.rxCb;
}

test('ping/pong round-trip', async () => {
  const { device, proto } = makePair();
  device.handler = (req) => {
    assert.equal(req.op, 'ping');
    return 'pong';
  };

  assert.equal(await proto.ping(), 'pong');
});

test('request id increments per call and matches in response', async () => {
  const { device, proto } = makePair();
  device.handler = () => null;

  await proto.ping();
  await proto.ping();
  await proto.ping();

  assert.deepEqual(device.recorded.map((r) => r.id), [1, 2, 3]);
});

test('error response rejects with error text', async () => {
  const { device, proto } = makePair();
  device.handler = () => { throw new Error('boom'); };

  await assert.rejects(proto.ping(), /boom/);
});

test('params are serialized into request payload', async () => {
  const { device, proto } = makePair();
  device.handler = () => null;

  await proto.writeScript('hello.be', 'def setup() end');

  assert.deepEqual(device.recorded[0], {
    op: 'script.write',
    id: 1,
    params: { filename: 'hello.be', code: 'def setup() end' },
  });
});

test('bytes split across many writes still parse on the device side', async () => {
  const { device, proto } = makePair();
  device.handler = (req) => {
    assert.equal(req.op, 'script.write');
    assert.equal((req.params.code as string).length, 2000);
    return null;
  };

  await proto.writeScript('big.be', 'X'.repeat(2000));

  assert.equal(device.recorded.length, 1);
});

test('concurrent calls do not interleave chunks on the wire', async () => {
  const { device, proto, transport } = makePair();
  device.handler = () => null;

  const origSend = transport.send.bind(transport);
  transport.send = async (chunk) => {
    await new Promise((resolve) => setTimeout(resolve, 0));
    return origSend(chunk);
  };

  await Promise.all([
    proto.writeScript('a.be', 'A'.repeat(500)),
    proto.writeScript('b.be', 'B'.repeat(500)),
  ]);

  assert.equal(device.recorded.length, 2);
  assert.equal(device.recorded[0].op, 'script.write');
  assert.equal(device.recorded[1].op, 'script.write');
  assert.equal(device.recorded[0].params.filename, 'a.be');
  assert.equal(device.recorded[1].params.filename, 'b.be');
});

test('signal push fires onSignal callback with right shape', () => {
  const { device, transport, proto } = makePair();
  const updates: unknown[] = [];
  proto.onSignal((s) => updates.push(s));

  device.push(rx(transport), {
    type: 'signal',
    name: 'VehicleSpeed',
    bus: 0,
    value: 42.5,
    prev: 42.0,
  });

  assert.deepEqual(updates, [
    { name: 'VehicleSpeed', bus: 0, value: 42.5, prev: 42.0 },
  ]);
});

test('log push fires onLog callback', () => {
  const { device, transport, proto } = makePair();
  const lines: Array<{ level: string; src: string; msg: string }> = [];
  proto.onLog((m) => lines.push(m));

  device.push(rx(transport), { type: 'log', level: 'info', src: 'device', msg: 'hello' });
  device.push(rx(transport), { type: 'log', level: 'info', src: 'device', msg: 'world' });

  assert.deepEqual(lines, [
    { level: 'info', src: 'device', msg: 'hello' },
    { level: 'info', src: 'device', msg: 'world' },
  ]);
});

test('trace push decodes hex data into Uint8Array', () => {
  const { device, transport, proto } = makePair();
  const frames: Array<{ bus: number; id: number; ts: number; data: Uint8Array }> = [];
  proto.onTrace((f) => frames.push(f));

  device.push(rx(transport), {
    type: 'trace',
    bus: 1,
    id: 0x123,
    ts: 12345,
    data: '0102DEADBEEF',
  });

  assert.equal(frames.length, 1);
  assert.equal(frames[0].bus, 1);
  assert.equal(frames[0].id, 0x123);
  assert.equal(frames[0].ts, 12345);
  assert.deepEqual(Array.from(frames[0].data), [0x01, 0x02, 0xde, 0xad, 0xbe, 0xef]);
});

test('subscribe, push, unsubscribe lifecycle', async () => {
  const { device, transport, proto } = makePair();
  const updates: unknown[] = [];
  proto.onSignal((s) => updates.push(s));

  let subscribed = false;
  device.handler = (req) => {
    if (req.op === 'signal.subscribe') subscribed = true;
    if (req.op === 'signal.unsubscribe') subscribed = false;
    return null;
  };

  await proto.subscribeSignal('Throttle', 0);
  assert.equal(subscribed, true);

  device.push(rx(transport), { type: 'signal', name: 'Throttle', bus: 0, value: 50, prev: 49 });

  await proto.unsubscribeSignal('Throttle', 0);
  assert.equal(subscribed, false);
  assert.equal(updates.length, 1);
});

test('high-level methods serialize correct ops', async () => {
  const { device, proto } = makePair();
  device.handler = () => null;

  await proto.traceStart([0, 1]);
  await proto.traceStop();
  await proto.setSimMode(true, 0);
  await proto.systemInfo();
  await proto.wifiSetCreds('ssid', 'psk');

  assert.deepEqual(device.recorded.map((r) => r.op), [
    'trace.start',
    'trace.stop',
    'sim.set',
    'system.info',
    'wifi.set_creds',
  ]);
  assert.deepEqual(device.recorded[0].params, { buses: [0, 1] });
  assert.deepEqual(device.recorded[2].params, { enabled: true, bus: 0 });
  assert.deepEqual(device.recorded[4].params, { ssid: 'ssid', psk: 'psk' });
});

test('unsolicited response with unknown id does not crash', async () => {
  const { device, transport, proto } = makePair();

  device.push(rx(transport), { id: 9999, ok: true, result: null });
  device.handler = () => 'pong';

  assert.equal(await proto.ping(), 'pong');
});

test('two frames in one chunk both deliver', () => {
  const { device, transport, proto } = makePair();
  const msgs: string[] = [];
  proto.onLog((m) => msgs.push(m.msg));

  const f1 = device.encode({ type: 'log', level: 'info', src: 'device', msg: 'a' });
  const f2 = device.encode({ type: 'log', level: 'info', src: 'device', msg: 'b' });
  const merged = new Uint8Array(f1.length + f2.length);
  merged.set(f1);
  merged.set(f2, f1.length);
  rx(transport)(merged);

  assert.deepEqual(msgs, ['a', 'b']);
});

test('frame split across rxCb deliveries reassembles', () => {
  const { device, transport, proto } = makePair();
  const msgs: string[] = [];
  proto.onLog((m) => msgs.push(m.msg));

  const full = device.encode({ type: 'log', level: 'info', src: 'device', msg: 'fragmented' });
  for (const b of full) {
    rx(transport)(new Uint8Array([b]));
  }

  assert.deepEqual(msgs, ['fragmented']);
});
