/*
 * Pure-Node round-trip test for the JSON-over-framed-bytes protocol.
 *
 * Why this exists: the Playwright UI smoke (ui-smoke.spec.ts) mocks the
 * Protocol responses inline — fast for clicking through views, but it does
 * NOT verify that the wire format itself is correct. If we drift the framing
 * (e.g. change the length prefix endianness) the smoke tests still pass.
 *
 * This file connects the real Protocol class to a fake Transport whose
 * send()/receive() pipe is implemented by an in-test "device" that speaks
 * the same wire format the firmware does. Any change that breaks the
 * encoding on either side will fail here.
 *
 * Doesn't touch the browser — runs in plain Node, so it survives any UI
 * redesign by the design team.
 */
import { test, expect } from '@playwright/test';
import { Protocol } from '../src/transport/protocol';
import type { Transport } from '../src/transport/types';

// ---------------------------------------------------------------------------
// In-test "device": speaks the same wire format the firmware does (4-byte LE
// length prefix + UTF-8 JSON). Tracks every request it received so tests
// can assert exact ops and params.
// ---------------------------------------------------------------------------

interface RecordedReq {
  op: string;
  id: number;
  params: Record<string, unknown>;
}

class FakeDevice {
  rx = new Uint8Array(0);
  recorded: RecordedReq[] = [];
  /** Set by tests to override default response per op. Return null = ok:true,result:null. */
  handler: ((req: RecordedReq) => unknown) = () => null;

  /** Called by the transport whenever the client writes bytes. */
  ingest(data: Uint8Array, deliver: (frame: Uint8Array) => void) {
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

      let result: unknown;
      try { result = this.handler(req); }
      catch (e: any) {
        deliver(this.encode({ id, ok: false, error: e.message ?? String(e) }));
        continue;
      }
      deliver(this.encode({ id, ok: true, result: result ?? null }));
    }
  }

  /** Push an unsolicited frame (log, signal, trace) to the client. */
  push(deliver: (frame: Uint8Array) => void, frame: object) {
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
  private rxCb: ((d: Uint8Array) => void) | null = null;
  constructor(private device: FakeDevice) {}
  async connect() {}
  async disconnect() { this.connected = false; }
  async send(data: Uint8Array) {
    this.device.ingest(data, (frame) => this.rxCb?.(frame));
  }
  onReceive(cb: (d: Uint8Array) => void) { this.rxCb = cb; }
}

function makePair() {
  const device = new FakeDevice();
  const transport = new FakeTransport(device);
  const proto = new Protocol(transport);
  return { device, transport, proto };
}

// ---------------------------------------------------------------------------
// Tests — each one targets a wire-level invariant.
// ---------------------------------------------------------------------------

test.describe('protocol wire format', () => {
  test('ping/pong round-trip', async () => {
    const { device, proto } = makePair();
    device.handler = (req) => {
      expect(req.op).toBe('ping');
      return 'pong';
    };
    expect(await proto.ping()).toBe('pong');
  });

  test('request id increments per call and matches in response', async () => {
    const { device, proto } = makePair();
    device.handler = () => null;
    await proto.ping();
    await proto.ping();
    await proto.ping();
    expect(device.recorded.map(r => r.id)).toEqual([1, 2, 3]);
  });

  test('error response rejects with error text', async () => {
    const { device, proto } = makePair();
    device.handler = () => { throw new Error('boom'); };
    await expect(proto.ping()).rejects.toThrow('boom');
  });

  test('params are serialized into request payload', async () => {
    const { device, proto } = makePair();
    device.handler = () => null;
    await proto.writeScript('hello.be', 'def setup() end');
    expect(device.recorded[0]).toMatchObject({
      op: 'script.write',
      params: { filename: 'hello.be', code: 'def setup() end' },
    });
  });

  test('bytes split across many writes still parse on the device side', async () => {
    // Real BLE chunks at ~100 B; this proves the device-side framing handles
    // a single frame arriving in many ingest() calls.
    const { device, proto } = makePair();
    device.handler = (req) => {
      expect(req.op).toBe('script.write');
      // Big code blob — forces the protocol's CHUNK loop to send multiple chunks.
      expect((req.params as any).code.length).toBe(2000);
      return null;
    };
    await proto.writeScript('big.be', 'X'.repeat(2000));
    // We expect Protocol.CHUNK = 100, so frame = 4 + json_len > 2000 = ~21 ingests.
    // What matters is exactly one request was reconstructed.
    expect(device.recorded).toHaveLength(1);
  });

  test('concurrent calls do not interleave chunks on the wire', async () => {
    const { device, proto, transport } = makePair();
    device.handler = () => null;

    // Override the fake transport to yield the event loop on every chunk,
    // which guarantees that concurrent send() loops will interleave if they
    // aren't synchronized.
    const origSend = transport.send.bind(transport);
    transport.send = async (chunk) => {
      await new Promise((r) => setTimeout(r, 0));
      return origSend(chunk);
    };

    // Fire two large requests at the same time. If they interleave, the FakeDevice
    // will see corrupt JSON or mismatched lengths, and it will drop or error them.
    const p1 = proto.writeScript('a.be', 'A'.repeat(500));
    const p2 = proto.writeScript('b.be', 'B'.repeat(500));

    await Promise.all([p1, p2]);

    expect(device.recorded).toHaveLength(2);
    expect(device.recorded[0].op).toBe('script.write');
    expect(device.recorded[1].op).toBe('script.write');
    expect(device.recorded[0].params.filename).toBe('a.be');
    expect(device.recorded[1].params.filename).toBe('b.be');
  });
});

test.describe('protocol push frames', () => {
  test('signal push fires onSignal callback with right shape', async () => {
    const { device, transport, proto } = makePair();
    const updates: any[] = [];
    proto.onSignal((s) => updates.push(s));

    // Fire a push frame as if the firmware just observed a value change.
    const cb = (transport as any).rxCb;
    device.push(cb, {
      type: 'signal',
      name: 'VehicleSpeed',
      bus: 0,
      value: 42.5,
      prev: 42.0,
    });

    expect(updates).toEqual([
      { name: 'VehicleSpeed', bus: 0, value: 42.5, prev: 42.0 },
    ]);
  });

  test('log push fires onLog callback', async () => {
    const { device, transport, proto } = makePair();
    const lines: string[] = [];
    proto.onLog((m) => lines.push(m));

    device.push((transport as any).rxCb, { type: 'log', msg: 'hello' });
    device.push((transport as any).rxCb, { type: 'log', msg: 'world' });

    expect(lines).toEqual(['hello', 'world']);
  });

  test('trace push decodes hex data into Uint8Array', async () => {
    const { device, transport, proto } = makePair();
    const frames: any[] = [];
    proto.onTrace((f) => frames.push(f));

    device.push((transport as any).rxCb, {
      type: 'trace', bus: 1, id: 0x123, ts: 12345, data: '0102DEADBEEF',
    });

    expect(frames).toHaveLength(1);
    expect(frames[0]).toMatchObject({ bus: 1, id: 0x123, ts: 12345 });
    expect(Array.from(frames[0].data)).toEqual([0x01, 0x02, 0xde, 0xad, 0xbe, 0xef]);
  });

  test('subscribe → push → unsubscribe lifecycle', async () => {
    const { device, transport, proto } = makePair();
    const updates: any[] = [];
    proto.onSignal((s) => updates.push(s));

    let subscribed = false;
    device.handler = (req) => {
      if (req.op === 'signal.subscribe') { subscribed = true; return null; }
      if (req.op === 'signal.unsubscribe') { subscribed = false; return null; }
      return null;
    };

    await proto.subscribeSignal('Throttle', 0);
    expect(subscribed).toBe(true);

    device.push((transport as any).rxCb, { type: 'signal', name: 'Throttle', bus: 0, value: 50, prev: 49 });

    await proto.unsubscribeSignal('Throttle', 0);
    expect(subscribed).toBe(false);
    expect(updates).toHaveLength(1);
  });

  test('high-level methods serialize correct ops', async () => {
    const { device, proto } = makePair();
    device.handler = () => null;
    await proto.traceStart([0, 1]);
    await proto.traceStop();
    await proto.setSimMode(true, 0);
    await proto.systemInfo();
    await proto.wifiSetCreds('ssid', 'psk');

    expect(device.recorded.map(r => r.op)).toEqual([
      'trace.start', 'trace.stop', 'sim.set', 'system.info', 'wifi.set_creds',
    ]);
    expect(device.recorded[0].params).toEqual({ buses: [0, 1] });
    expect(device.recorded[2].params).toEqual({ enabled: true, bus: 0 });
    expect(device.recorded[4].params).toEqual({ ssid: 'ssid', psk: 'psk' });
  });
});

test.describe('protocol robustness', () => {
  test('unsolicited response (unknown id) does not crash', async () => {
    const { device, transport, proto } = makePair();
    // Push a response with no matching pending request.
    device.push((transport as any).rxCb, { id: 9999, ok: true, result: null });
    // Subsequent calls should still work.
    device.handler = () => 'pong';
    expect(await proto.ping()).toBe('pong');
  });

  test('two frames in one chunk both deliver', async () => {
    const { device, transport, proto } = makePair();
    const lines: string[] = [];
    proto.onLog((m) => lines.push(m));

    // Concat two frames into one rxCb delivery.
    const f1 = device.encode({ type: 'log', msg: 'a' });
    const f2 = device.encode({ type: 'log', msg: 'b' });
    const merged = new Uint8Array(f1.length + f2.length);
    merged.set(f1); merged.set(f2, f1.length);
    (transport as any).rxCb(merged);

    expect(lines).toEqual(['a', 'b']);
  });

  test('frame split across rxCb deliveries reassembles', async () => {
    const { device, transport, proto } = makePair();
    const lines: string[] = [];
    proto.onLog((m) => lines.push(m));

    const full = device.encode({ type: 'log', msg: 'fragmented' });
    // Hand the bytes to the client one at a time.
    for (const b of full) {
      (transport as any).rxCb(new Uint8Array([b]));
    }
    expect(lines).toEqual(['fragmented']);
  });
});
