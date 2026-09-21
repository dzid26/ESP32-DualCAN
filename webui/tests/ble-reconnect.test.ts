// Store-level auto-reconnect behavior. The transport is mocked; the backoff
// delay is injected so the suite never waits the real ~22 s.
//
// Store code imports extensionless bundler-style specifiers, so the resolver
// shim must load before the dynamic import below.
import './helpers/register-ts-resolver.ts';
import test from 'node:test';
import assert from 'node:assert/strict';

const storeModule = await import('../src/lib/store.svelte.ts');
const { AppState, RECONNECT_BACKOFF_MS } = storeModule;
const { toast } = await import('../src/lib/toast.svelte.ts');

const shownToasts: Array<{ severity?: string; message: string }> = [];
toast.show = ((opts: { severity?: string; message: string }) => {
  shownToasts.push(opts);
  return 0;
}) as typeof toast.show;

type DisconnectKind = 'user' | 'auth_fail' | 'replaced' | 'unexpected';

/** Scriptable stand-in for `BleTransport`. */
function makeTransport() {
  const state = {
    connected: false,
    connectCalls: 0,
    reconnectCalls: [] as Array<{ allowPicker?: boolean }>,
    restartCalls: 0,
    connCbs: [] as Array<(c: boolean) => void>,
    discCbs: [] as Array<(k: DisconnectKind) => void>,
    reconnectImpl: async (_opts: { allowPicker?: boolean }): Promise<void> => {},
    restartImpl: async (): Promise<void> => {},
  };
  const transport = {
    get connected() { return state.connected; },
    get deviceName(): string | null { return null; },
    onConnectionChange(cb: (c: boolean) => void) { state.connCbs.push(cb); },
    onDisconnect(cb: (k: DisconnectKind) => void) { state.discCbs.push(cb); },
    async connect() { state.connectCalls++; state.connected = true; state.connCbs.forEach((cb) => cb(true)); },
    async disconnect() { state.connected = false; state.connCbs.forEach((cb) => cb(false)); },
    async send() {},
    onReceive() {},
    async restartNotifications() { state.restartCalls++; await state.restartImpl(); },
    async reconnect(opts: { allowPicker?: boolean } = {}) {
      state.reconnectCalls.push(opts);
      await state.reconnectImpl(opts);
    },
    /** Emulate the plugin's disconnect callback (transport already set to false). */
    emit(kind: DisconnectKind) {
      state.connected = false;
      state.connCbs.forEach((cb) => cb(false));
      state.discCbs.forEach((cb) => cb(kind));
    },
  };
  return { transport, state };
}

function makeApp() {
  const { transport, state } = makeTransport();
  const sleeps: number[] = [];
  const app = new AppState({
    ble: transport,
    sleep: async (ms: number) => { sleeps.push(ms); },
  });
  return { app, transport, state, sleeps };
}

/** Like {@link makeApp}, but each injected sleep blocks until the test
 *  releases it — proves exactly when an attempt is allowed to run. */
function makePacedApp() {
  const { transport, state } = makeTransport();
  const sleeps: number[] = [];
  const releases: Array<() => void> = [];
  const app = new AppState({
    ble: transport,
    sleep: (ms: number) => new Promise<void>((resolve) => {
      sleeps.push(ms);
      releases.push(resolve);
    }),
  });
  return { app, transport, state, sleeps, releases };
}

/** Let queued microtasks (and one macrotask) drain without real waiting. */
async function settle(): Promise<void> {
  for (let i = 0; i < 100; i++) await Promise.resolve();
  await new Promise((r) => setImmediate(r));
  for (let i = 0; i < 25; i++) await Promise.resolve();
}

test('unexpected drop retries the backoff schedule, then surfaces the Connect prompt', async () => {
  const { app, transport, state, sleeps } = makeApp();
  state.reconnectImpl = async () => { throw new Error('not in range'); };

  transport.emit('unexpected');
  await settle();

  assert.deepEqual(sleeps, [...RECONNECT_BACKOFF_MS]);
  assert.equal(state.reconnectCalls.length, RECONNECT_BACKOFF_MS.length);
  assert.ok(state.reconnectCalls.every((c) => c.allowPicker === false), 'never falls back to the chooser');
  assert.equal(app.reconnecting, false, 'reconnecting clears after exhaustion');
  assert.match(app.connectError ?? '', /tap Connect to reconnect/i);
  assert.ok(shownToasts.some((t) => /did not come back/i.test(t.message)));
});

for (const kind of ['unexpected', 'auth_fail', 'replaced'] as const) {
  test(`reconnect is attempted on a '${kind}' drop`, async () => {
    const { app, transport, state } = makeApp();
    state.reconnectImpl = async () => { throw new Error('down'); };

    transport.emit(kind);
    await settle();

    assert.ok(state.reconnectCalls.length >= 1, `expected a reconnect for '${kind}'`);
    assert.equal(app.reconnecting, false);
  });
}

test('user-initiated disconnect does not reconnect', async () => {
  const { app, transport, state } = makeApp();

  transport.emit('user');
  await settle();

  assert.equal(state.reconnectCalls.length, 0);
  assert.equal(app.reconnecting, false);
});

test('stops retrying once a reconnect re-establishes the link', async () => {
  const { app, transport, state, sleeps } = makeApp();
  state.reconnectImpl = async () => { app.connected = true; };

  transport.emit('unexpected');
  await settle();

  assert.equal(state.reconnectCalls.length, 1);
  assert.deepEqual(sleeps, [RECONNECT_BACKOFF_MS[0]]);
  assert.equal(app.reconnecting, false);
});

test('never reconnects immediately: the first attempt waits out the first backoff', async () => {
  const { app, transport, state, sleeps, releases } = makePacedApp();
  state.reconnectImpl = async () => { throw new Error('peer not ready yet'); };

  transport.emit('unexpected');
  await settle();

  assert.deepEqual(sleeps, [RECONNECT_BACKOFF_MS[0]], 'the store sleeps before attempting');
  assert.equal(state.reconnectCalls.length, 0, 'no attempt may run before the first delay elapses');
  assert.equal(app.reconnecting, true);

  releases.shift()!();
  await settle();

  assert.equal(state.reconnectCalls.length, 1, 'attempt 1 runs once the first delay elapses');
  assert.deepEqual(sleeps, [RECONNECT_BACKOFF_MS[0], RECONNECT_BACKOFF_MS[1]], 'failure schedules the next backoff');
  assert.equal(app.reconnecting, true, 'still retrying');
});

test('a failed attempt is retried only after the next backoff, then success ends the loop', async () => {
  const { app, transport, state, sleeps, releases } = makePacedApp();
  const toastCount = shownToasts.length;
  let attempts = 0;
  state.reconnectImpl = async () => {
    attempts++;
    if (attempts === 1) throw new Error('too soon after the drop');
    app.connected = true;
  };

  transport.emit('unexpected');
  await settle();
  releases.shift()!();
  await settle();

  assert.equal(state.reconnectCalls.length, 1);
  assert.deepEqual(sleeps, [RECONNECT_BACKOFF_MS[0], RECONNECT_BACKOFF_MS[1]]);
  assert.equal(app.reconnecting, true, 'a failed attempt keeps the loop running');

  releases.shift()!();
  await settle();

  assert.equal(state.reconnectCalls.length, 2, 'the retry runs only after the next backoff');
  assert.deepEqual(sleeps, [RECONNECT_BACKOFF_MS[0], RECONNECT_BACKOFF_MS[1]], 'success stops further delays');
  assert.equal(app.reconnecting, false);
  assert.equal(shownToasts.length, toastCount, 'success suppresses the give-up toast');
});

// ---- Reboot-context routing (OTA/reboot `suppressUnexpectedDisconnect`) ----

for (const kind of ['unexpected', 'auth_fail'] as const) {
  test(`an expected OTA/reboot '${kind}' drop uses the reboot reconnect copy`, async () => {
    const { app, transport, state, sleeps } = makeApp();
    const toastCount = shownToasts.length;
    state.reconnectImpl = async () => { throw new Error('still rebooting'); };

    // rebootDevice()/doOTA() set this before the reboot drops the link.
    app.suppressUnexpectedDisconnect = true;
    transport.emit(kind);
    await settle();

    assert.equal(app.suppressUnexpectedDisconnect, false, 'the expected-drop flag is consumed');
    assert.equal(app.reconnecting, false);
    assert.deepEqual(sleeps, [...RECONNECT_BACKOFF_MS], 'reboot retries share the normal backoff schedule');
    assert.equal(state.reconnectCalls.length, RECONNECT_BACKOFF_MS.length);
    assert.ok(state.reconnectCalls.every((c) => c.allowPicker === false));
    assert.ok(
      app.logs.some((l) => /Expected BLE drop \(device rebooting\)/.test(l.msg)),
      'the drop is classified as the expected reboot',
    );
    assert.ok(
      app.logs.some((l) => /Waiting for device to reboot and re-advertise/.test(l.msg)),
      'attemptReconnect ran with the reboot context',
    );
    assert.match(app.connectError ?? '', /^Device rebooted — tap Connect to reconnect\.$/);

    const toasts = shownToasts.slice(toastCount);
    assert.ok(
      toasts.some((t) => /Device rebooted but did not come back automatically/.test(t.message)),
      'the give-up toast uses reboot copy',
    );
    assert.ok(
      !toasts.some((t) => /rejected the connection|taken over by another client/.test(t.message)),
      'no generic drop toast while the reboot is expected',
    );
  });
}

// ---- Concurrent-drop guard ----

test('a second surprise drop during an in-flight reconnect cannot start a second loop', async () => {
  const { app, transport, state, sleeps, releases } = makePacedApp();
  state.reconnectImpl = async () => { throw new Error('still down'); };

  transport.emit('unexpected');
  await settle();
  assert.equal(app.reconnecting, true);
  assert.deepEqual(sleeps, [RECONNECT_BACKOFF_MS[0]]);

  // The plugin can fire another disconnect while the loop is sleeping.
  transport.emit('unexpected');
  await settle();

  assert.deepEqual(sleeps, [RECONNECT_BACKOFF_MS[0]], 'the extra drop schedules no second backoff');
  assert.equal(state.reconnectCalls.length, 0, 'the extra drop starts no second loop');

  const toastCount = shownToasts.length;
  while (releases.length) {
    releases.shift()!();
    await settle();
  }

  assert.deepEqual(sleeps, [...RECONNECT_BACKOFF_MS], 'exactly one full backoff schedule ran');
  assert.equal(state.reconnectCalls.length, RECONNECT_BACKOFF_MS.length, 'attempt count is not doubled');
  assert.equal(app.reconnecting, false);
  assert.equal(shownToasts.length, toastCount + 1, 'a single give-up toast, not one per drop');
});

// ---- Stalled-notification recovery (`recoverStalledTransport`) ----

test('a failed re-subscribe falls through to a chooser-less full reconnect', async () => {
  const { app, state } = makeApp();
  app.connected = true; // GATT link is up; only the notification stream is stalled
  state.restartImpl = async () => { throw new Error('subscribe failed'); };
  state.reconnectImpl = async () => { /* link restored */ };

  await (app as any).recoverStalledTransport();

  assert.equal(state.restartCalls, 1, 'level 1 re-subscribes once');
  assert.equal(state.reconnectCalls.length, 1, 'level 2 reconnects the transport');
  assert.equal(state.reconnectCalls[0].allowPicker, false, 'no chooser without a user gesture');
  assert.ok(app.logs.some((l) => /notification stream stalled/.test(l.msg)));
  assert.ok(app.logs.some((l) => /reconnecting BLE transport/.test(l.msg)));
  assert.ok(!app.logs.some((l) => /BLE reconnect failed/.test(l.msg)));
});

test('a re-subscribe that pings cleanly recovers without reconnecting', async () => {
  const { app, state } = makeApp();
  app.connected = true;
  state.restartImpl = async () => {};
  app.proto.ping = async () => 'pong';

  await (app as any).recoverStalledTransport();

  assert.equal(state.restartCalls, 1);
  assert.equal(state.reconnectCalls.length, 0, 'the gentle level-1 recovery is enough');
  assert.ok(app.logs.some((l) => /BLE transport recovered/.test(l.msg)));
});

test('a stalled stream whose post-resubscribe ping fails reconnects the transport', async () => {
  const { app, state } = makeApp();
  app.connected = true;
  state.restartImpl = async () => {};
  app.proto.ping = async () => { throw new Error('no response'); };
  state.reconnectImpl = async () => {};

  await (app as any).recoverStalledTransport();

  assert.equal(state.restartCalls, 1);
  assert.equal(state.reconnectCalls.length, 1);
  assert.equal(state.reconnectCalls[0].allowPicker, false);
});

test('stalled-stream recovery is a no-op while the store is disconnected', async () => {
  const { app, state } = makeApp();

  await (app as any).recoverStalledTransport();

  assert.equal(state.restartCalls, 0);
  assert.equal(state.reconnectCalls.length, 0);
  assert.equal(app.logs.length, 0);
});

test('stalled-stream recovery ignores a concurrent stall report while one is in flight', async () => {
  const { app, state } = makeApp();
  app.connected = true;
  let releaseRestart!: () => void;
  const restartGate = new Promise<void>((resolve) => { releaseRestart = resolve; });
  state.restartImpl = () => restartGate;
  app.proto.ping = async () => 'pong';

  const first = (app as any).recoverStalledTransport() as Promise<void>;
  await settle();
  const second = (app as any).recoverStalledTransport() as Promise<void>;
  await settle();

  assert.equal(state.restartCalls, 1, 'the in-flight guard drops the concurrent report');
  releaseRestart();
  await first;
  await second;
  assert.equal(state.restartCalls, 1);
  assert.equal(state.reconnectCalls.length, 0);
});

// ---- Mid-sleep link recovery ----

test('a link restored during the backoff sleep cancels the pending reconnect attempt', async () => {
  const { app, transport, state, sleeps, releases } = makePacedApp();
  state.reconnectImpl = async () => { throw new Error('should never be attempted'); };

  transport.emit('unexpected');
  await settle();
  assert.equal(app.reconnecting, true);
  assert.deepEqual(sleeps, [RECONNECT_BACKOFF_MS[0]]);

  // The transport re-established the link while the store was sleeping —
  // onConnChange(true) is what sets this flag in production.
  app.connected = true;
  releases.shift()!();
  await settle();

  assert.equal(state.reconnectCalls.length, 0, 'the attempt is skipped once the link is back');
  assert.deepEqual(sleeps, [RECONNECT_BACKOFF_MS[0]], 'no further backoff is scheduled');
  assert.equal(app.reconnecting, false);
});

// ---- toggleConnect lockout ----

test('connect toggle is locked out while an automatic reconnect is in flight', async () => {
  const { app, transport, state, releases } = makePacedApp();
  state.reconnectImpl = async () => { throw new Error('still down'); };
  transport.connect = async () => { state.connectCalls++; };

  transport.emit('unexpected');
  await settle();
  assert.equal(app.reconnecting, true);

  await app.toggleConnect();
  await settle();

  assert.equal(state.connectCalls, 0, 'the locked toggle must not start a user connect');
  assert.equal(app.connecting, false);

  while (releases.length) {
    releases.shift()!();
    await settle();
  }
  assert.equal(app.reconnecting, false);

  await app.toggleConnect();
  await settle();
  assert.equal(state.connectCalls, 1, 'the toggle works again once the reconnect loop ends');
});
